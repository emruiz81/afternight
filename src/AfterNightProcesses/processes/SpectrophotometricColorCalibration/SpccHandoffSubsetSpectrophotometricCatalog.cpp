// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The ImageIntegration hand-off to SPCC: the workflow archive that names an image, the sources its registration
/// recorded, the requirements they are held to and the sources that hold.

#include "stdafx.hpp"

#include "SpccHandoffSubsetSpectrophotometricCatalog.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>
#include <vector>

namespace an::ui::processes
{

namespace
{

/// The integration group whose output a workflow archive names as the image.
struct ArchiveMatch
{
    QString manifestPath;
    QString outputDirectory;
    QString integrationGroupKey;
    QString registrationGroupKey;
};

[[nodiscard]] QString normalizedAbsolutePath(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return {};
    }
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

[[nodiscard]] std::optional<QJsonObject> readJsonObject(const QString& path, QStringList& diagnostics)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        diagnostics << QCoreApplication::translate("SpccProcessReadiness",
                                                   "ImageIntegration handoff: could not read workflow archive %1.")
                           .arg(path);
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        diagnostics << QCoreApplication::translate("SpccProcessReadiness",
                                                   "ImageIntegration handoff: workflow archive %1 is not valid JSON.")
                           .arg(path);
        return std::nullopt;
    }
    return document.object();
}

/// The workflow archives of an output directory, the newest first: its own and every one of its history runs'.
[[nodiscard]] std::vector<QFileInfo> workflowArchiveManifestFiles(const QString& outputDirectory)
{
    std::vector<QFileInfo> manifests;
    const QFileInfo direct(QDir(outputDirectory).filePath(QStringLiteral("workflow_manifest.json")));
    if (direct.exists() && direct.isFile())
    {
        manifests.push_back(direct);
    }
    const QDir historyDir(QDir(outputDirectory).filePath(QStringLiteral("history")));
    for (const QFileInfo& run : historyDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time))
    {
        const QFileInfo manifest(QDir(run.absoluteFilePath()).filePath(QStringLiteral("workflow_manifest.json")));
        if (manifest.exists() && manifest.isFile())
        {
            manifests.push_back(manifest);
        }
    }
    std::sort(manifests.begin(), manifests.end(),
              [](const QFileInfo& left, const QFileInfo& right) { return left.lastModified() > right.lastModified(); });
    return manifests;
}

[[nodiscard]] std::optional<ArchiveMatch> findWorkflowArchiveMatch(const QString& outputDirectory,
                                                                   const QString& targetPath, QStringList& diagnostics)
{
    const QString targetAbsolutePath = normalizedAbsolutePath(targetPath);
    const QString targetFileName = QFileInfo(targetPath).fileName();
    for (const QFileInfo& manifestInfo : workflowArchiveManifestFiles(outputDirectory))
    {
        const std::optional<QJsonObject> root = readJsonObject(manifestInfo.absoluteFilePath(), diagnostics);
        if (!root.has_value())
        {
            continue;
        }
        const QJsonObject manifest = root->value(QStringLiteral("manifest")).isObject()
                                         ? root->value(QStringLiteral("manifest")).toObject()
                                         : *root;
        const QString stated = manifest.value(QStringLiteral("outputDirectory")).toString(outputDirectory).trimmed();
        const QString resolvedOutput = stated.isEmpty() ? outputDirectory : stated;
        for (const QJsonValue& groupValue : manifest.value(QStringLiteral("integrationGroups")).toArray())
        {
            const QJsonObject group = groupValue.toObject();
            const QString outputFilename = group.value(QStringLiteral("outputFilename")).toString().trimmed();
            if (outputFilename.isEmpty())
            {
                continue;
            }
            const QString outputAbsolutePath = normalizedAbsolutePath(QDir(resolvedOutput).filePath(outputFilename));
            if (outputAbsolutePath != targetAbsolutePath && outputFilename != targetFileName)
            {
                continue;
            }
            return ArchiveMatch{
                .manifestPath = manifestInfo.absoluteFilePath(),
                .outputDirectory = QFileInfo(resolvedOutput).absoluteFilePath(),
                .integrationGroupKey = group.value(QStringLiteral("groupKey")).toString(),
                .registrationGroupKey = group.value(QStringLiteral("registrationGroupKey")).toString(),
            };
        }
    }
    return std::nullopt;
}

} // namespace

QString spccSolutionIdentity(const an::WcsSolution& solution)
{
    const an::Result<std::vector<an::CodecCard>> cards = an::buildWcsCards(solution);
    if (!cards.hasValue() || cards->empty())
    {
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    digest.addData(QByteArray::number(solution.imageWidth()));
    digest.addData(QByteArrayLiteral("x"));
    digest.addData(QByteArray::number(solution.imageHeight()));
    for (const an::CodecCard& card : *cards)
    {
        digest.addData(QByteArrayLiteral("\n"));
        digest.addData(QByteArray::fromStdString(card.keyword()));
        digest.addData(QByteArrayLiteral("="));
        digest.addData(QByteArray::fromStdString(card.value()));
    }
    return QStringLiteral("an-wcs:%1").arg(QString::fromLatin1(digest.result().toHex()));
}

QString spccObservationEpochText(const std::optional<double>& epochYear)
{
    return epochYear.has_value() ? QString::number(*epochYear, 'f', 6) : QString();
}

SpccIntegrationHandoff locateSpccIntegrationHandoff(const QString& targetPath, const bool fallbackRegionQueryAllowed)
{
    SpccIntegrationHandoff handoff;
    handoff.targetPath = normalizedAbsolutePath(targetPath);
    handoff.fallbackRegionQueryAllowed = fallbackRegionQueryAllowed;
    if (handoff.targetPath.isEmpty())
    {
        handoff.diagnostics << QCoreApplication::translate(
            "SpccProcessReadiness",
            "ImageIntegration handoff: no file-backed target path is available; using the normal WCS region query.");
        return handoff;
    }

    handoff.outputDirectory = QFileInfo(handoff.targetPath).absoluteDir().absolutePath();
    const std::optional<ArchiveMatch> match =
        findWorkflowArchiveMatch(handoff.outputDirectory, handoff.targetPath, handoff.diagnostics);
    if (!match.has_value() || match->registrationGroupKey.trimmed().isEmpty())
    {
        handoff.diagnostics << QCoreApplication::translate(
            "SpccProcessReadiness",
            "ImageIntegration handoff: no workflow archive matched this target; using the normal WCS region query.");
        return handoff;
    }

    handoff.located = true;
    handoff.manifestPath = match->manifestPath;
    handoff.outputDirectory = match->outputDirectory;
    handoff.integrationGroupKey = match->integrationGroupKey;
    handoff.registrationGroupKey = match->registrationGroupKey;
    handoff.metricsDatabasePath = QDir(handoff.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    const QFileInfo metrics(handoff.metricsDatabasePath);
    if (!metrics.exists() || !metrics.isFile())
    {
        handoff.diagnostics << QCoreApplication::translate(
                                   "SpccProcessReadiness",
                                   "ImageIntegration handoff: registration_metrics.db was not found for workflow "
                                   "archive %1; using the normal WCS region query.")
                                   .arg(handoff.manifestPath);
        return handoff;
    }

    an::Result<IntegrationMatchedSources> sources =
        readIntegrationMatchedSources(handoff.metricsDatabasePath, handoff.registrationGroupKey);
    if (!sources.hasValue())
    {
        handoff.diagnostics << QCoreApplication::translate(
                                   "SpccProcessReadiness",
                                   "ImageIntegration handoff: could not load matched Gaia source_id rows: %1. Using "
                                   "the normal WCS region query.")
                                   .arg(QString::fromUtf8(sources.status().message()));
        return handoff;
    }
    handoff.settingsVersion = sources->settingsVersion;
    handoff.references = std::move(sources->references);
    handoff.loaded = true;
    handoff.diagnostics << QCoreApplication::translate(
                               "SpccProcessReadiness",
                               "ImageIntegration handoff: loaded %1 matched Gaia source_id references from %2 "
                               "(registration group %3, settings v%4).")
                               .arg(static_cast<qulonglong>(handoff.references.size()))
                               .arg(handoff.metricsDatabasePath, handoff.registrationGroupKey)
                               .arg(handoff.settingsVersion);
    if (handoff.references.empty())
    {
        handoff.diagnostics << QCoreApplication::translate(
            "SpccProcessReadiness",
            "ImageIntegration handoff: no persisted source IDs were available; using the normal WCS region query.");
    }
    return handoff;
}

an::MatchedSourceRequirements spccHandoffRequirements(const QString& solutionIdentity,
                                                      const std::optional<double>& epochYear)
{
    an::MatchedSourceRequirements requirements;
    requirements.requiredProperMotionModelVersion = kSpccProperMotionModelVersion;
    requirements.requiredWcsSolutionId = solutionIdentity.toStdString();
    requirements.requiredObservationEpoch = spccObservationEpochText(epochYear).toStdString();
    requirements.maximumResidualPixels = kSpccHandoffMaximumResidualPixels;
    requirements.maximumSeparationArcseconds = kSpccHandoffMaximumSeparationArcseconds;
    return requirements;
}

std::vector<std::uint64_t> acceptedHandoffSources(const an::MatchedSourceResolution& resolution,
                                                  const an::SpectrumCatalog& catalog)
{
    std::vector<std::uint64_t> sources;
    for (const an::MatchedSourceLookup& lookup : resolution.lookups)
    {
        if (lookup.status != an::MatchedSourceLookupStatus::Satisfied || !lookup.recordIndex.has_value())
        {
            continue;
        }
        if (const an::SpectrumRecord* record = catalog.record(*lookup.recordIndex); record != nullptr)
        {
            sources.push_back(record->sourceIdentifier);
        }
    }
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    return sources;
}

QString spccHandoffResolutionText(const SpccIntegrationHandoff& handoff, const an::MatchedSourceResolution& resolution,
                                  const an::SpectrumCatalogManifest& manifest)
{
    return QCoreApplication::translate(
               "SpccProcessReadiness",
               "ImageIntegration handoff: accepted %1 of %2 persisted Gaia source_id references "
               "for SPCC catalog manifest %3; region-query fallback is %4.")
        .arg(static_cast<qulonglong>(resolution.acceptedCount))
        .arg(static_cast<qulonglong>(handoff.references.size()))
        .arg(QString::fromStdString(manifest.manifestId))
        .arg(handoff.fallbackRegionQueryAllowed ? QCoreApplication::translate("SpccProcessReadiness", "allowed")
                                                : QCoreApplication::translate("SpccProcessReadiness", "disabled"));
}

} // namespace an::ui::processes
