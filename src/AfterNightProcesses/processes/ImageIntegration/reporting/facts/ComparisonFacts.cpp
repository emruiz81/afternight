// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include "processes/ImageIntegration/reporting/ReportFigures.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimeZone>

#include <algorithm>
#include <utility>

/// @file
/// A run beside the earlier runs of its output folder.
///
/// Everything stated of an earlier run comes from the report that run's archive keeps: its figures are the ones it
/// stated when it was committed, so a comparison reads no image, no manifest and no metrics database of another run,
/// and an archive that keeps no report this version reads is compared with nothing and listed by its id.
///
/// A master is compared with the latest earlier run that made it, which in a folder of incremental runs is not the
/// run before: a run that found a master still valid made none. The run as a whole is compared with the latest
/// earlier run that completed.

namespace ImageIntegration::Reporting
{
[[nodiscard]] std::optional<double> averageFiniteValues(const QList<double>& values)
{
    if (values.isEmpty())
    {
        return std::nullopt;
    }

    double total = 0.0;
    int count = 0;
    for (const auto value : values)
    {
        if (!std::isfinite(value))
        {
            continue;
        }
        total += value;
        ++count;
    }

    if (count <= 0)
    {
        return std::nullopt;
    }
    return total / static_cast<double>(count);
}

ReportBaseline masterFigures(const ReportMaster& master)
{
    ReportBaseline figures;
    bool fatesStated = false;
    int used = 0;
    double seconds = 0.0;
    QList<double> widths;
    for (const ReportFrame& frame : master.frames)
    {
        fatesStated = fatesStated || frame.state.has_value();
        if (frame.state != ReportFrameState::Used)
        {
            continue;
        }
        ++used;
        seconds += frame.exposureSeconds.value_or(0.0);
        if (frame.fwhm.has_value() && std::isfinite(*frame.fwhm))
        {
            widths.append(*frame.fwhm);
        }
    }
    if (!master.frames.empty())
    {
        figures.framesTaken = static_cast<int>(master.frames.size());
    }
    if (fatesStated)
    {
        figures.frames = used;
        figures.integrationMs = static_cast<qint64>(std::llround(seconds * 1000.0));
    }
    figures.backgroundNoise = master.snapshot.backgroundNoise;
    figures.snr = master.snapshot.snr;
    figures.fwhm = medianOf(widths);
    if (master.stack.has_value())
    {
        figures.effectiveFrames = master.stack->effectiveFrames;
        if (master.stack->rejection.has_value())
        {
            figures.rejectedFraction = master.stack->rejection->global;
        }
    }
    return figures;
}

namespace
{
/// Whether a master's file is on disk when its run ends: the run made it, or it was still valid.
[[nodiscard]] bool masterIsThere(const ReportMaster& master)
{
    return master.state == ReportMasterState::Made || master.state == ReportMasterState::Reused;
}
} // namespace

ReportRunTotals runTotals(const ReportModel& model)
{
    ReportRunTotals totals;
    totals.frames = model.run.frames;
    totals.integrationMs = model.run.capturedMs;
    totals.framesUsed = model.run.framesUsed;
    totals.integratedMs = model.run.integratedMs;

    QList<double> snr;
    QList<double> overlap;
    for (const ReportMaster& master : model.masters)
    {
        if (!masterIsThere(master))
        {
            continue;
        }
        ++totals.masters;
        totals.outputBytes += master.bytes.value_or(0);
        if (master.snapshot.snr.has_value())
        {
            snr.append(*master.snapshot.snr);
        }
        if (master.overlapRetained.has_value())
        {
            overlap.append(*master.overlapRetained);
        }
    }
    totals.snr = averageFiniteValues(snr);
    totals.overlapRetained = averageFiniteValues(overlap);
    return totals;
}

namespace
{
const char* const kStageCalibrationMasters = QT_TRANSLATE_NOOP("ImageIntegrationReport", "Calibration masters");
const char* const kStageLightsCalibration = QT_TRANSLATE_NOOP("ImageIntegrationReport", "Lights calibration");
const char* const kStageRegistration = QT_TRANSLATE_NOOP("ImageIntegrationReport", "Registration");
const char* const kStageStacking = QT_TRANSLATE_NOOP("ImageIntegrationReport", "Stacking");
const char* const kStageRun = QT_TRANSLATE_NOOP("ImageIntegrationReport", "The run");

/// Whether a run ended with every master made or still valid.
[[nodiscard]] bool completed(const ReportModel& model)
{
    return model.run.outcome == ReportOutcome::Completed || model.run.outcome == ReportOutcome::CompletedWithWarnings;
}

/// Whether two masters' frames are of different shapes: their aspect ratios differ by more than one percent. Two
/// masters of which either was not measured are taken to be of one shape.
[[nodiscard]] bool shapesDiffer(const ReportMaster& now, const ReportMaster& before)
{
    if (!now.statistics.has_value() || !before.statistics.has_value())
    {
        return false;
    }
    const auto ratio = [](const ReportImageStatistics& statistics) -> std::optional<double> {
        if (statistics.width == 0u || statistics.height == 0u)
        {
            return std::nullopt;
        }
        return static_cast<double>(statistics.width) / static_cast<double>(statistics.height);
    };
    const std::optional<double> own = ratio(*now.statistics);
    const std::optional<double> earlier = ratio(*before.statistics);
    return own.has_value() && earlier.has_value() && std::abs(*own - *earlier) > 0.01 * *own;
}

/// The place in this run's folder a picture of an earlier run is copied to, beside the master's own pictures.
[[nodiscard]] QString copiedPicture(const ReportMaster& master, const char* name)
{
    return QStringLiteral("report_previews/%1/%2").arg(master.previewFolder, QLatin1String(name));
}

/// The most earlier runs a master is set beside, the latest among them: what the document's switch holds.
constexpr std::size_t kBaselinesAtMost = 6;

/// Each master the run made beside the latest earlier run that made it, and the runs that made it before that
/// one. A master no earlier run made is new; a master the run reused is the earlier run's own and is compared
/// with nothing; with no earlier run nothing is.
void fillBaselines(ReportBuild& build, ReportModel& model)
{
    const std::vector<StoredRun>& runs = build.earlierRuns();
    if (runs.empty())
    {
        return;
    }
    const QDir history(build.historyDirectory());
    for (ReportMaster& master : model.masters)
    {
        if (master.state != ReportMasterState::Made)
        {
            continue;
        }
        ReportBaseline baseline;
        std::vector<ReportBaseline> earlier;
        for (const StoredRun& run : runs)
        {
            const auto made =
                std::find_if(run.model.masters.begin(), run.model.masters.end(), [&master](const ReportMaster& other) {
                    return other.key == master.key && other.state == ReportMasterState::Made;
                });
            if (made == run.model.masters.end())
            {
                continue;
            }
            ReportBaseline figures = masterFigures(*made);
            figures.runId = run.runId;
            figures.archivedAt = run.model.run.archivedAt;
            figures.made = true;
            figures.sideBySide = shapesDiffer(master, *made);
            const QDir archive(history.filePath(run.runId));
            const bool hasPreview = !made->preview.trimmed().isEmpty() && build.exists(archive.filePath(made->preview));
            const bool hasDetail = !made->detail.trimmed().isEmpty() && build.exists(archive.filePath(made->detail));
            if (!baseline.made)
            {
                // The latest run that made the master. Its pictures are the ones that run archived, each under its
                // own display stretch; they are copied into this run's folder when its report is stored, so the
                // report stays whole once the history drops that run.
                if (hasPreview)
                {
                    figures.preview = copiedPicture(master, kPreviousMasterPictureName);
                }
                if (hasDetail)
                {
                    figures.detail = copiedPicture(master, kPreviousDetailPictureName);
                }
                baseline = std::move(figures);
                continue;
            }
            // A run that made the master before that one. Its pictures stay where its own archive holds them, beside
            // this run's folder: copying every earlier run's pictures into every run would fill the history with
            // copies.
            if (hasPreview)
            {
                figures.preview = QStringLiteral("../%1/%2").arg(run.runId, made->preview.trimmed());
            }
            if (hasDetail)
            {
                figures.detail = QStringLiteral("../%1/%2").arg(run.runId, made->detail.trimmed());
            }
            earlier.push_back(std::move(figures));
            if (earlier.size() + 1u == kBaselinesAtMost)
            {
                break;
            }
        }
        master.baseline = std::move(baseline);
        master.earlierBaselines = std::move(earlier);
    }
}

/// What a calibration master's kind is called.
[[nodiscard]] QString calibrationKindName(const ReportCalibrationKind kind)
{
    switch (kind)
    {
    case ReportCalibrationKind::Bias:
        return reportText("Bias");
    case ReportCalibrationKind::Dark:
        return reportText("Dark");
    case ReportCalibrationKind::Flat:
        return reportText("Flat");
    }
    return {};
}

/// A calibration master as a change names it: its kind, then its group's name.
[[nodiscard]] QString calibrationName(const ReportCalibrationMaster& master)
{
    const QString kind = calibrationKindName(master.kind);
    return master.name.trimmed().isEmpty() ? kind : kind + QLatin1Char(' ') + master.name;
}

/// Whether two calibration masters are the same group in two runs: a bias group's key may be spelled as a dark's.
[[nodiscard]] bool sameCalibrationGroup(const ReportCalibrationMaster& one, const ReportCalibrationMaster& other)
{
    return one.kind == other.kind && one.key == other.key;
}

/// The settings of one group, or of the run, with the step the group belongs to.
struct SettingsOwner
{
    const char* stage = nullptr;
    QString key;
    QString name;
    const std::vector<ReportSettingsBlock>* blocks = nullptr;

    /// What tells the group from every other of its step: its key, with its kind before it for a calibration
    /// master.
    QString identity;
};

/// Every group of a model that states settings, step by step in the order the run works, then the run itself.
[[nodiscard]] std::vector<SettingsOwner> settingsOwners(const ReportModel& model)
{
    std::vector<SettingsOwner> owners;
    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        owners.push_back({kStageCalibrationMasters, master.key, calibrationName(master), &master.settings,
                          QStringLiteral("%1|%2").arg(static_cast<int>(master.kind)).arg(master.key)});
    }
    for (const ReportLightGroup& group : model.lightGroups)
    {
        owners.push_back({kStageLightsCalibration, group.key, group.name, &group.settings, group.key});
    }
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        owners.push_back({kStageRegistration, group.key, group.name, &group.settings, group.key});
    }
    for (const ReportMaster& master : model.masters)
    {
        owners.push_back({kStageStacking, master.key, master.name, &master.settings, master.key});
    }
    owners.push_back({kStageRun, {}, {}, &model.run.settings, {}});
    return owners;
}

[[nodiscard]] const SettingsOwner* ownerOf(const std::vector<SettingsOwner>& owners, const SettingsOwner& like)
{
    const auto found = std::find_if(owners.begin(), owners.end(), [&like](const SettingsOwner& owner) {
        return owner.stage == like.stage && owner.identity == like.identity;
    });
    return found == owners.end() ? nullptr : &*found;
}

[[nodiscard]] const ReportSettingsBlock* blockTitled(const std::vector<ReportSettingsBlock>& blocks,
                                                     const QString& title)
{
    const auto found = std::find_if(blocks.begin(), blocks.end(),
                                    [&title](const ReportSettingsBlock& block) { return block.title == title; });
    return found == blocks.end() ? nullptr : &*found;
}

[[nodiscard]] const ReportSettingsRow* rowNamed(const ReportSettingsBlock* block, const QString& name)
{
    if (block == nullptr)
    {
        return nullptr;
    }
    const auto found = std::find_if(block->rows.begin(), block->rows.end(),
                                    [&name](const ReportSettingsRow& row) { return row.name == name; });
    return found == block->rows.end() ? nullptr : &*found;
}

/// The names of two lists as one: the first list's in its order, then the second's that the first has not.
template <class Items, class NameOf>
[[nodiscard]] QStringList mergedNames(const Items* first, const Items* second, NameOf&& nameOf)
{
    QStringList names;
    for (const Items* items : {first, second})
    {
        if (items == nullptr)
        {
            continue;
        }
        for (const auto& item : *items)
        {
            if (const QString name = nameOf(item); !names.contains(name))
            {
                names.append(name);
            }
        }
    }
    return names;
}

/// The settings of one group that differ between the two runs, block by block and name by name. A setting only one
/// run states is a change with the other side empty; a setting both state the same is counted and not listed.
void compareSettings(const SettingsOwner& now, const SettingsOwner& before, ReportComparison& comparison)
{
    const QStringList titles =
        mergedNames(now.blocks, before.blocks, [](const ReportSettingsBlock& block) { return block.title; });
    for (const QString& title : titles)
    {
        const ReportSettingsBlock* own = blockTitled(*now.blocks, title);
        const ReportSettingsBlock* earlier = blockTitled(*before.blocks, title);
        const QStringList names =
            mergedNames(own != nullptr ? &own->rows : nullptr, earlier != nullptr ? &earlier->rows : nullptr,
                        [](const ReportSettingsRow& row) { return row.name; });
        for (const QString& name : names)
        {
            const ReportSettingsRow* is = rowNamed(own, name);
            const ReportSettingsRow* was = rowNamed(earlier, name);
            if (is != nullptr && was != nullptr)
            {
                ++comparison.settingsCompared;
                if (is->value == was->value)
                {
                    continue;
                }
            }
            comparison.settingsChanged.push_back(ReportSettingChange{.stage = reportText(now.stage),
                                                                     .groupKey = now.key,
                                                                     .group = now.name,
                                                                     .block = title,
                                                                     .name = name,
                                                                     .before = was != nullptr ? was->value : QString(),
                                                                     .after = is != nullptr ? is->value : QString()});
        }
    }
}

/// What differs in the frames of one group both runs have: the files this run has and the earlier one had not, and
/// the reverse, by path.
void compareInputs(const char* stage, const QString& key, const QString& name, const QStringList& now,
                   const QStringList& before, ReportComparison& comparison)
{
    const QSet<QString> own(now.begin(), now.end());
    const QSet<QString> earlier(before.begin(), before.end());
    ReportInputChange change{.stage = reportText(stage), .groupKey = key, .group = name, .added = {}, .removed = {}};
    for (const QString& path : now)
    {
        if (!earlier.contains(path))
        {
            change.added.append(QFileInfo(path).fileName());
        }
    }
    for (const QString& path : before)
    {
        if (!own.contains(path))
        {
            change.removed.append(QFileInfo(path).fileName());
        }
    }
    if (!change.added.isEmpty() || !change.removed.isEmpty())
    {
        comparison.inputsChanged.push_back(std::move(change));
    }
}

[[nodiscard]] QStringList lightPaths(const ReportMaster& master)
{
    QStringList paths;
    paths.reserve(static_cast<qsizetype>(master.frames.size()));
    for (const ReportFrame& frame : master.frames)
    {
        paths.append(frame.path);
    }
    return paths;
}

/// What changed between an earlier run and this one: the settings of the groups both have, the groups only one has,
/// and the frames the groups both have gained and lost.
void fillChanges(const ReportModel& model, const ReportModel& earlier, ReportComparison& comparison)
{
    const std::vector<SettingsOwner> now = settingsOwners(model);
    const std::vector<SettingsOwner> before = settingsOwners(earlier);
    for (const SettingsOwner& owner : now)
    {
        if (const SettingsOwner* same = ownerOf(before, owner); same != nullptr)
        {
            compareSettings(owner, *same, comparison);
        }
        else
        {
            comparison.groupsChanged.push_back(ReportGroupChange{
                .stage = reportText(owner.stage), .groupKey = owner.key, .group = owner.name, .added = true});
        }
    }
    for (const SettingsOwner& owner : before)
    {
        if (ownerOf(now, owner) == nullptr)
        {
            comparison.groupsChanged.push_back(ReportGroupChange{
                .stage = reportText(owner.stage), .groupKey = owner.key, .group = owner.name, .added = false});
        }
    }

    for (const ReportCalibrationMaster& master : model.calibrationMasters)
    {
        const auto same = std::find_if(
            earlier.calibrationMasters.begin(), earlier.calibrationMasters.end(),
            [&master](const ReportCalibrationMaster& other) { return sameCalibrationGroup(master, other); });
        if (same != earlier.calibrationMasters.end())
        {
            compareInputs(kStageCalibrationMasters, master.key, calibrationName(master), master.inputFiles,
                          same->inputFiles, comparison);
        }
    }
    for (const ReportMaster& master : model.masters)
    {
        const auto same = std::find_if(earlier.masters.begin(), earlier.masters.end(),
                                       [&master](const ReportMaster& other) { return other.key == master.key; });
        if (same != earlier.masters.end())
        {
            compareInputs(kStageStacking, master.key, master.name, lightPaths(master), lightPaths(*same), comparison);
        }
    }
}

/// The run as a whole beside the latest earlier run that completed.
void fillRunComparison(ReportBuild& build, ReportModel& model)
{
    for (const StoredRun& run : build.earlierRuns())
    {
        if (!completed(run.model))
        {
            continue;
        }
        ReportComparison comparison;
        comparison.baselineRunId = run.runId;
        comparison.baselineArchivedAt = run.model.run.archivedAt;
        comparison.current = runTotals(model);
        comparison.previous = runTotals(run.model);
        comparison.previousApplicationVersion = run.model.run.applicationVersion;
        comparison.previousFingerprint = run.model.run.fingerprint;
        comparison.previousSettingsFingerprint = run.model.run.settingsFingerprint;
        comparison.previousInputsFingerprint = run.model.run.inputsFingerprint;
        fillChanges(model, run.model, comparison);
        model.comparison = std::move(comparison);
        return;
    }
}

/// The earlier runs of the folder, oldest first, each as its stored report states it, or by its id alone with why
/// its archive gave no report.
void fillHistory(ReportBuild& build, ReportModel& model)
{
    const std::vector<EarlierArchive>& archives = build.earlierArchives();
    if (archives.empty())
    {
        return;
    }
    const std::vector<StoredRun>& runs = build.earlierRuns();
    const QDir history(build.historyDirectory());
    const auto first = std::find_if(model.masters.begin(), model.masters.end(), masterIsThere);
    const QString noiseOf = first == model.masters.end() ? QString() : first->key;

    model.history.reserve(archives.size());
    for (auto archive = archives.rbegin(); archive != archives.rend(); ++archive)
    {
        ReportHistoryEntry entry;
        entry.runId = archive->runId;
        if (build.exists(history.filePath(archive->runId + QLatin1Char('/') + QLatin1String(kWorkflowReportFileName))))
        {
            entry.report = QStringLiteral("../%1/%2").arg(archive->runId, QLatin1String(kWorkflowReportFileName));
        }
        if (!archive->stored.has_value())
        {
            entry.refusal = archive->refusal;
            model.history.push_back(std::move(entry));
            continue;
        }
        const ReportModel& earlier = runs[*archive->stored].model;
        entry.archivedAt = earlier.run.archivedAt;
        entry.outcome = earlier.run.outcome;
        entry.framesUsed = earlier.run.framesUsed;
        entry.frames = earlier.run.frames;
        entry.integrationMs = earlier.run.integratedMs;
        entry.masters = static_cast<int>(std::count_if(earlier.masters.begin(), earlier.masters.end(), masterIsThere));
        const auto same = std::find_if(earlier.masters.begin(), earlier.masters.end(),
                                       [&noiseOf](const ReportMaster& master) { return master.key == noiseOf; });
        if (!noiseOf.isEmpty() && same != earlier.masters.end() && masterIsThere(*same))
        {
            entry.backgroundNoise = same->snapshot.backgroundNoise;
        }
        model.history.push_back(std::move(entry));
    }
}
} // namespace

void fillComparison(ReportBuild& build, ReportModel& model)
{
    if (build.inputs().runDirectory.trimmed().isEmpty())
    {
        // A run with no folder of its own has no history beside it.
        return;
    }
    fillBaselines(build, model);
    fillRunComparison(build, model);
    fillHistory(build, model);
}
} // namespace ImageIntegration::Reporting
