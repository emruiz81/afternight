// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportModelBuilder.h"

#include "facts/ReportFacts.h"

#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"

#include "ReportArchiveReader.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace ImageIntegration::Reporting
{
ReportBuild::ReportBuild(const ReportModelInputs& inputs, const ReportSources& sources)
    : m_inputs(inputs), m_sources(sources)
{
}

QString ReportBuild::resolvePath(const QString& path) const
{
    return resolvePathAgainstBase(path, m_inputs.runDirectory);
}

QString ReportBuild::outputFile(const IntegrationGroupResult& group) const
{
    QString file = resolvePath(group.outputPath);
    if (file.isEmpty() && !group.outputFilename.trimmed().isEmpty())
    {
        file = resolvePath(QDir(m_inputs.manifest.outputDirectory).filePath(group.outputFilename));
    }
    return file;
}

bool ReportBuild::exists(const QString& path) const
{
    const QString resolved = resolvePath(path);
    return !resolved.isEmpty() && m_sources.files != nullptr && m_sources.files->exists(resolved);
}

std::optional<qint64> ReportBuild::fileSize(const QString& path) const
{
    const QString resolved = resolvePath(path);
    if (resolved.isEmpty() || m_sources.files == nullptr)
    {
        return std::nullopt;
    }
    return m_sources.files->size(resolved);
}

std::optional<ReportImageMeasurement> ReportBuild::measure(const QString& path, const QString& what)
{
    if (path.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (const auto found = m_measured.constFind(key); found != m_measured.cend())
    {
        return found.value();
    }
    std::optional<ReportImageMeasurement> measured;
    const bool there = m_sources.files == nullptr || m_sources.files->exists(key);
    if (there && m_sources.images != nullptr)
    {
        measured = m_sources.images->measure(key, core::parallelCoreWidth(m_inputs.manifest.maxParallelCores));
    }
    if (!measured.has_value())
    {
        imageNotStated(reportText("The statistics and the sample distribution of %1").arg(what), key, there);
    }
    m_measured.insert(key, measured);
    return measured;
}

void ReportBuild::imageNotStated(const QString& subject, const QString& file, const bool there)
{
    if (!there)
    {
        notStated(subject, reportText("The file %1 is no longer there.").arg(QFileInfo(file).fileName()));
    }
    else if (m_sources.images == nullptr || m_sources.session == nullptr)
    {
        notStated(subject, reportText("The report was built without an engine session, so no image was read."));
    }
    else
    {
        notStated(subject, reportText("The file %1 could not be read or measured.").arg(QFileInfo(file).fileName()));
    }
}

std::optional<ReportImageLevels> ReportBuild::levels(const QString& path, const QString& what)
{
    if (path.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (const auto found = m_levels.constFind(key); found != m_levels.cend())
    {
        return found.value();
    }
    std::optional<ReportImageLevels> measured;
    const bool there = m_sources.files == nullptr || m_sources.files->exists(key);
    if (there && m_sources.images != nullptr)
    {
        measured = m_sources.images->measureLevels(key, core::parallelCoreWidth(m_inputs.manifest.maxParallelCores));
    }
    if (!measured.has_value())
    {
        imageNotStated(reportText("The level and the noise of %1").arg(what), key, there);
    }
    m_levels.insert(key, measured);
    return measured;
}

std::optional<ReportImageStatistics> ReportBuild::statistics(const QString& path, const QString& what)
{
    std::optional<ReportImageMeasurement> measured = measure(path, what);
    return measured.has_value() ? std::optional<ReportImageStatistics>(std::move(measured->statistics)) : std::nullopt;
}

std::optional<ReportHeader> ReportBuild::header(const QString& path)
{
    if (path.trimmed().isEmpty() || m_sources.headers == nullptr)
    {
        return std::nullopt;
    }
    if (const auto found = m_headers.constFind(path); found != m_headers.cend())
    {
        return found.value();
    }
    // A file that is not there has no header: asking the codec would only spend an admission to learn it.
    std::optional<ReportHeader> probed;
    if (m_sources.files == nullptr || m_sources.files->exists(path))
    {
        probed = m_sources.headers->probe(path);
    }
    m_headers.insert(path, probed);
    return probed;
}

int ReportBuild::utcOffsetSeconds() const
{
    const QJsonValue stated = m_inputs.context.value(QStringLiteral("utcOffsetSeconds"));
    if (stated.isDouble())
    {
        return stated.toInt();
    }
    return m_sources.host != nullptr ? m_sources.host->now().offsetFromUtc() : 0;
}

QString ReportBuild::referenceFrame(const QString& registrationGroupKey)
{
    if (const auto found = m_references.constFind(registrationGroupKey); found != m_references.cend())
    {
        return found.value();
    }
    QString reference;
    if (const std::optional<int> version = settingsVersion(registrationGroupKey); version.has_value())
    {
        reference = m_sources.metrics->referenceFrame(registrationGroupKey, *version);
    }
    m_references.insert(registrationGroupKey, reference);
    return reference;
}

QString ReportBuild::historyDirectory() const
{
    const QString runDirectory = m_inputs.runDirectory.trimmed();
    return runDirectory.isEmpty() ? QString() : QFileInfo(QDir::cleanPath(runDirectory)).dir().absolutePath();
}

const std::vector<StoredRun>& ReportBuild::earlierRuns()
{
    if (m_earlierRuns.has_value())
    {
        return *m_earlierRuns;
    }
    m_earlierRuns.emplace();
    const QString runDirectory = m_inputs.runDirectory.trimmed();
    if (runDirectory.isEmpty() || m_sources.files == nullptr)
    {
        return *m_earlierRuns;
    }
    // The runs of a folder are named by when they started, so a name that sorts first is an earlier run. The run
    // being written and any run after it are never read.
    const QFileInfo own(QDir::cleanPath(runDirectory));
    const QDir history = own.dir();
    QStringList names = m_sources.files->folders(history.absolutePath());
    std::sort(names.begin(), names.end(), std::greater<>());
    for (const QString& name : names)
    {
        if (name >= own.fileName())
        {
            continue;
        }
        // A stored report is a run's only in a folder that holds the run's manifest, which a commit writes last. A
        // commit that failed after its report was stored, and whose folder could not be removed, left a report of
        // a run whose outputs were put back: it is read by nothing, and the folder is listed with why.
        const QDir folder(history.filePath(name));
        if (m_sources.files->exists(folder.filePath(QLatin1String(kStoredReportFileName))) &&
            !m_sources.files->exists(folder.filePath(QLatin1String(kRunManifestFileName))))
        {
            m_earlierArchives.push_back(
                EarlierArchive{.runId = name,
                               .stored = std::nullopt,
                               .refusal = reportText("The folder keeps a report and no manifest: the run's archive was "
                                                     "not committed, so its report is not read.")});
            continue;
        }
        ReportArchiveRead read = ReportArchiveReader::read(history.filePath(name), *m_sources.files);
        EarlierArchive archive{.runId = name, .stored = std::nullopt, .refusal = read.why};
        if (read.model.has_value())
        {
            archive.stored = m_earlierRuns->size();
            m_earlierRuns->push_back(StoredRun{.runId = name, .model = std::move(*read.model)});
        }
        m_earlierArchives.push_back(std::move(archive));
    }
    return *m_earlierRuns;
}

const std::vector<EarlierArchive>& ReportBuild::earlierArchives()
{
    static_cast<void>(earlierRuns());
    return m_earlierArchives;
}

void ReportBuild::notStated(const QString& what, const QString& why)
{
    const ReportNotStated entry{.what = what, .why = why};
    if (std::find(m_notStated.begin(), m_notStated.end(), entry) == m_notStated.end())
    {
        m_notStated.push_back(entry);
    }
}

bool ReportBuild::hasMetrics() const
{
    return m_sources.metrics != nullptr && m_sources.metrics->available();
}

std::optional<int> ReportBuild::settingsVersion(const QString& registrationGroupKey)
{
    if (!hasMetrics())
    {
        return std::nullopt;
    }
    if (const auto found = m_settingsVersions.constFind(registrationGroupKey); found != m_settingsVersions.cend())
    {
        return found.value();
    }
    const std::optional<int> version = m_sources.metrics->settingsVersion(registrationGroupKey);
    m_settingsVersions.insert(registrationGroupKey, version);
    return version;
}

const QHash<QString, RegistrationMetricRow>& ReportBuild::metrics(const QString& registrationGroupKey)
{
    if (const auto found = m_metrics.constFind(registrationGroupKey); found != m_metrics.cend())
    {
        return found.value();
    }
    QHash<QString, RegistrationMetricRow> rows;
    if (const std::optional<int> version = settingsVersion(registrationGroupKey); version.has_value())
    {
        rows = m_sources.metrics->metricsByFrame(registrationGroupKey, *version);
    }
    return *m_metrics.insert(registrationGroupKey, std::move(rows));
}

ReportModel buildReportModel(const ReportModelInputs& inputs, const ReportSources& sources)
{
    ReportBuild build(inputs, sources);
    ReportModel model;
    // The order matters: the lights and the settings are stated on the masters and the groups, the lineage joins
    // what the three areas state, the run's rollups read what the masters measured, and the issues read it all.
    fillMasters(build, model);
    fillFrames(build, model);
    fillCalibration(build, model);
    fillRegistration(build, model);
    fillLineage(build, model);
    fillSettings(build, model);
    fillRun(build, model);
    fillDisk(build, model);
    fillComparison(build, model);
    fillIssues(build, model);
    model.notStated = build.notStatedEntries();
    return model;
}
} // namespace ImageIntegration::Reporting
