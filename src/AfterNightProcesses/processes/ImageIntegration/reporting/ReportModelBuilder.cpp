// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportModelBuilder.h"

#include "facts/ReportFacts.h"

#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"

#include <QDir>
#include <QFileInfo>

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

std::optional<ReportImageStatistics> ReportBuild::measure(const QString& path)
{
    if (path.trimmed().isEmpty() || m_sources.images == nullptr)
    {
        return std::nullopt;
    }
    const QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (const auto found = m_measured.constFind(key); found != m_measured.cend())
    {
        return found.value();
    }
    const std::optional<ReportImageStatistics> measured =
        m_sources.images->measure(key, core::parallelCoreWidth(m_inputs.manifest.maxParallelCores));
    m_measured.insert(key, measured);
    return measured;
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
    std::optional<ReportHeader> probed = m_sources.headers->probe(path);
    m_headers.insert(path, probed);
    return probed;
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
    // The order matters: the lights and the settings are stated on the masters and the groups, and the run's
    // rollups read what the masters measured.
    fillMasters(build, model);
    fillFrames(build, model);
    fillCalibration(build, model);
    fillRegistration(build, model);
    fillSettings(build, model);
    fillRun(build, model);
    fillComparison(build, model);
    return model;
}
} // namespace ImageIntegration::Reporting
