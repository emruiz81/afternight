// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's drizzle stage: each drizzled group's lights described on the canvas
/// their registration and the group's canvas mode place them on, one descriptor document per light carrying the
/// engine's identity of its geometry, dense coordinates beside it when the group keeps checkpoints, and the group
/// sidecar listing them, every document published whole.

#include "stdafx.hpp"
#include "LightsDrizzleStage.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>

#include <AfterNightPlatform/Logging.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{

std::vector<const WorkflowManifest::IntegrationGroup*> groupsToProcess(const WorkflowManifest& manifest)
{
    std::vector<const WorkflowManifest::IntegrationGroup*> groups;
    for (const auto& group : manifest.integrationGroups)
    {
        if (!group.drizzle.has_value() || !group.drizzle->enabled)
        {
            continue;
        }
        if (manifest.executionMode == ExecutionMode::Incremental &&
            std::find(manifest.incrementalIntegrationGroups.begin(), manifest.incrementalIntegrationGroups.end(),
                      group.groupKey) == manifest.incrementalIntegrationGroups.end())
        {
            continue;
        }
        groups.push_back(&group);
    }
    return groups;
}

[[nodiscard]] QString documentBaseFor(const FrameId& frame)
{
    QString base = ImageIntegration::sanitizeFilename(QFileInfo(frame.canonicalPath()).completeBaseName());
    if (base.isEmpty())
    {
        base = QStringLiteral("frame");
    }
    const QByteArray hash =
        QCryptographicHash::hash(frame.canonicalPath().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("%1_%2").arg(base, QString::fromLatin1(hash.left(8)));
}

[[nodiscard]] QString persistencePolicyKey(const WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy policy)
{
    return policy == WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint
               ? QStringLiteral("DenseCheckpoint")
               : QStringLiteral("CompactDescriptorPreferred");
}

/// The fingerprints a group's sidecar states and the planner recomputes.
struct DrizzleFingerprints
{
    QString transforms;
    QString sourceDistortions;
    QString sourceInputs;
};

[[nodiscard]] DrizzleFingerprints fingerprintsOf(const ImageIntegration::DrizzleGroupGeometry& geometry,
                                                 const WorkflowManifest::IntegrationGroup& group,
                                                 const ExecutionPreparationSnapshot* snapshot)
{
    std::unordered_map<QString, an::RegistrationTransform> transforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
    std::unordered_map<QString, std::optional<an::SipDistortion>> resolved;
    for (const auto& frame : geometry.frames)
    {
        const QString key = frame.frame.canonicalPath();
        transforms.emplace(key, frame.transform);
        provenance.emplace(key, frame.provenance);
        resolved.emplace(key, frame.resolvedDistortion);
    }
    return {stage_utils::fingerprintTransforms(transforms),
            stage_utils::fingerprintSourceDistortions(transforms, provenance, resolved),
            computeFrameFileStampFingerprint(group.lightFiles, snapshot)};
}

/// One light's descriptor as it is now, with the engine's identity of its geometry.
struct ExpectedDescriptor
{
    ImageIntegration::DrizzleDescriptorDocument document;
    an::DrizzleMapDescriptor descriptor;
};

[[nodiscard]] an::Result<ExpectedDescriptor> expectedDescriptorOf(const ImageIntegration::DrizzleFrameGeometry& frame,
                                                                  const an::DrizzleCanvas& canvas, const double scale)
{
    ImageIntegration::DrizzleDescriptorDocument document;
    document.frameId = frame.frame.canonicalPath();
    document.sourceWidth = frame.width;
    document.sourceHeight = frame.height;
    document.outputWidth = canvas.width;
    document.outputHeight = canvas.height;
    document.scale = scale;
    document.transform = frame.transform;
    document.sourceDistortion = frame.provenance;
    document.resolvedSourceDistortion = frame.resolvedDistortion;
    document.postTranslation = canvas.origin;
    document.normalizationSpace = an::DrizzleNormalizationSpace::SourcePixels;
    // The identity is of the geometry as the document states it, which is what a reader rebuilds: one computed over the
    // geometry in memory does not survive the document's text exactly, and every reader would refuse it.
    QString error;
    std::optional<ImageIntegration::DrizzleDescriptorDocument> stated =
        ImageIntegration::parseDrizzleDescriptor(ImageIntegration::formatDrizzleDescriptor(document), error);
    if (!stated.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidData, error.toStdString());
    }
    document = std::move(*stated);
    an::Result<an::DrizzleMapDescriptor> descriptor = ImageIntegration::drizzleMapDescriptorOf(document);
    if (!descriptor.hasValue())
    {
        return descriptor.status();
    }
    an::Result<QByteArray> identity =
        ImageIntegration::drizzleEngineIdentityOf(*descriptor, document.normalizationSpace);
    if (!identity.hasValue())
    {
        return identity.status();
    }
    document.engineIdentity = std::move(*identity);
    return ExpectedDescriptor{std::move(document), std::move(*descriptor)};
}

/// Whether a group's descriptors on disk describe the geometry, inputs and settings asked for.
[[nodiscard]] bool descriptorsCurrent(const ImageIntegration::DrizzleDescriptorIndex& index,
                                      const WorkflowManifest::IntegrationGroup& group,
                                      const ImageIntegration::DrizzleGroupGeometry& geometry,
                                      const DrizzleFingerprints& fingerprints,
                                      const std::vector<ExpectedDescriptor>& expected, QString& reason)
{
    const auto& drizzle = *group.drizzle;
    const QJsonObject& meta = index.sidecar;
    // The sidecar names the kernel as the group spelled it when it was written; another spelling is the same kernel.
    using ImageIntegration::RegistrationHashUtils::fingerprintChoice;
    if (fingerprintChoice(ImageIntegration::PersistedChoice::DrizzleKernel,
                          meta.value(QStringLiteral("kernel")).toString()) !=
            fingerprintChoice(ImageIntegration::PersistedChoice::DrizzleKernel, drizzle.kernel) ||
        meta.value(QStringLiteral("cfaAware")).toBool() != drizzle.cfaAware ||
        meta.value(QStringLiteral("cfaPattern")).toString() != drizzle.cfaPattern ||
        std::abs(meta.value(QStringLiteral("scale")).toDouble() - static_cast<double>(drizzle.scale)) > 1e-6 ||
        std::abs(meta.value(QStringLiteral("pixelFraction")).toDouble() - static_cast<double>(drizzle.pixelFraction)) >
            1e-6)
    {
        reason = QStringLiteral("The drizzle settings changed.");
        return false;
    }
    if (meta.value(QStringLiteral("transformsFingerprint")).toString() != fingerprints.transforms ||
        meta.value(QStringLiteral("sourceDistortionFingerprint")).toString() != fingerprints.sourceDistortions)
    {
        reason = QStringLiteral("The registration changed.");
        return false;
    }
    if (meta.value(QStringLiteral("sourceInputsFingerprint")).toString() != fingerprints.sourceInputs)
    {
        reason = QStringLiteral("The lights changed.");
        return false;
    }
    if (meta.value(QStringLiteral("cometProductsFingerprint")).toString() != geometry.cometProductsFingerprint)
    {
        reason = QStringLiteral("The comet products changed.");
        return false;
    }
    if (index.outputWidth != geometry.canvas.width || index.outputHeight != geometry.canvas.height ||
        index.byFrame.size() != expected.size())
    {
        reason = QStringLiteral("The drizzle canvas or its lights changed.");
        return false;
    }
    const bool denseRequired = drizzle.mapPersistencePolicy ==
                               WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint;
    for (const ExpectedDescriptor& wanted : expected)
    {
        const auto found = index.byFrame.find(FrameId(wanted.document.frameId).identityKey());
        if (found == index.byFrame.end() || found->second.document.engineIdentity != wanted.document.engineIdentity)
        {
            reason = QStringLiteral("The geometry of %1 changed.").arg(wanted.document.frameId);
            return false;
        }
        if (denseRequired && found->second.densePayloadPath.isEmpty())
        {
            reason = QStringLiteral("Dense checkpoints are asked for and %1 has none.").arg(wanted.document.frameId);
            return false;
        }
    }
    reason = QStringLiteral("Every descriptor describes the current registration, lights and settings.");
    return true;
}

/// A light's dense map: the output position of each source pixel's centre in integer pixel centres, two Float32 per
/// pixel, row-major, a pair of quiet NaNs where it maps nowhere.
[[nodiscard]] QByteArray denseMapOf(const an::DrizzleMapDescriptor& descriptor, const std::uint32_t width,
                                    const std::uint32_t height, const std::stop_token& stop, bool& stopped)
{
    QByteArray samples(static_cast<qsizetype>(static_cast<std::uint64_t>(width) * height * 2u * sizeof(float)),
                       Qt::Uninitialized);
    auto* out = reinterpret_cast<float*>(samples.data());
    constexpr float kUnmapped = std::numeric_limits<float>::quiet_NaN();
    for (std::uint32_t y = 0; y < height; ++y)
    {
        if (stop.stop_requested())
        {
            stopped = true;
            return {};
        }
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const an::Result<an::PixelCoordinate> mapped =
                an::mapDrizzlePoint(descriptor, {static_cast<double>(x), static_cast<double>(y)});
            const bool valid = mapped.hasValue() && std::isfinite(mapped->x) && std::isfinite(mapped->y);
            *out++ = valid ? static_cast<float>(mapped->x) : kUnmapped;
            *out++ = valid ? static_cast<float>(mapped->y) : kUnmapped;
        }
    }
    return samples;
}

[[nodiscard]] StageResult publicationFailure(const ImageIntegration::DocumentPublicationResult& published,
                                             const QString& path)
{
    if (published.failure == ImageIntegration::DocumentPublicationFailure::Cancelled)
    {
        return StageResult::cancelled();
    }
    if (published.failure == ImageIntegration::DocumentPublicationFailure::DiskFull)
    {
        return StageResult::failure(IntegrationError::diskFull(path));
    }
    return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                 QStringLiteral("Failed to publish a drizzle document"), path,
                                                 published.message));
}

} // namespace

QString LightsDrizzleStage::name() const
{
    return QStringLiteral("LightsDrizzle");
}

WorkflowStage LightsDrizzleStage::stageId() const
{
    return WorkflowStage::LightsDrizzle;
}

bool LightsDrizzleStage::shouldSkip(const WorkflowManifest& manifest) const
{
    return groupsToProcess(manifest).empty();
}

QString LightsDrizzleStage::skipReason(const WorkflowManifest& manifest) const
{
    const bool anyDrizzle = std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                                        [](const WorkflowManifest::IntegrationGroup& group) {
                                            return group.drizzle.has_value() && group.drizzle->enabled;
                                        });
    if (!anyDrizzle)
    {
        return QStringLiteral("No drizzle groups enabled.");
    }
    if (manifest.executionMode == ExecutionMode::Incremental && groupsToProcess(manifest).empty())
    {
        return QStringLiteral("No dirty drizzle groups in incremental plan.");
    }
    return QStringLiteral("No work required.");
}

StageResult LightsDrizzleStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                        ProgressReporter& reporter)
{
    const auto groups = groupsToProcess(manifest);
    if (groups.empty())
    {
        return StageResult::skipped();
    }
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return StageResult::failure(
            IntegrationError(IntegrationErrorCode::InvalidOutputPath, QStringLiteral("Output directory is required")));
    }
    const ImageIntegration::WorkflowStageServices* services = stageServices();
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(
            QStringLiteral("The lights drizzle stage runs only inside a workflow run, which lends it the session")));
    }
    const std::stop_token stop = services->stopToken();

    const int totalGroups = static_cast<int>(groups.size());
    reporter.logger().stageStarted(name(), totalGroups);
    QElapsedTimer stageTimer;
    stageTimer.start();
    int groupIndex = 0;
    IntegrationErrorReport errorReport;

    for (const auto* groupPtr : groups)
    {
        ++groupIndex;
        const auto& group = *groupPtr;
        const QString groupLabel = group.groupKey.toCompactString();
        reporter.logger().groupStarted(groupIndex, totalGroups, groupLabel);
        QElapsedTimer groupTimer;
        groupTimer.start();
        bool abortStage = false;

        const auto processGroup = [&]() -> StageResult {
            if (token.isCancellationRequested() || stop.stop_requested())
            {
                return StageResult::cancelled();
            }
            if (group.lightFiles.empty())
            {
                reporter.logger().operationDetail(QStringLiteral("Skipping"),
                                                  QStringLiteral("No light frames in group"));
                return StageResult::success();
            }
            const auto& drizzle = *group.drizzle;
            reporter.logger().operationDetail(QStringLiteral("Drizzle settings"),
                                              stage_utils::describeDrizzleSettings(drizzle));
            stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups, 0.0f,
                                             QStringLiteral("Preparing drizzle descriptors"), groupLabel);
            if (!std::isfinite(drizzle.scale) || drizzle.scale < 1.0f || drizzle.scale > 4.0f)
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                     QStringLiteral("Drizzle scale must be between 1.0 and 4.0")));
            }
            if (!std::isfinite(drizzle.pixelFraction) || drizzle.pixelFraction <= 0.0f || drizzle.pixelFraction > 1.0f)
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                     QStringLiteral("Drizzle pixel fraction must be within (0.0, 1.0]")));
            }
            an::DrizzleKernel kernel = an::DrizzleKernel::Square;
            if (!stage_utils::parseKernel(drizzle.kernel, kernel))
            {
                return StageResult::failure(IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                                             QStringLiteral("Drizzle kernel is invalid"), {},
                                                             drizzle.kernel));
            }
            if (ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode))
            {
                abortStage = true;
            }

            ImageIntegration::DrizzleGeometryResolution resolution = ImageIntegration::resolveDrizzleGroupGeometry(
                *services, manifest, group, executionCounterAccumulator());
            if (resolution.cancelled)
            {
                return StageResult::cancelled();
            }
            if (!resolution.geometry.has_value())
            {
                return StageResult::failure(resolution.error);
            }
            const ImageIntegration::DrizzleGroupGeometry& geometry = *resolution.geometry;
            for (const QString& missing : geometry.withoutTransform)
            {
                reporter.logger().warning(
                    QStringLiteral("%1 has no registration transform and is left out of the drizzle.")
                        .arg(QFileInfo(missing).fileName()));
            }
            if (geometry.intersectionReplaced)
            {
                reporter.logger().warning(stage_utils::intersectionReplacedWarning(groupLabel));
            }
            reporter.logger().operationDetail(
                QStringLiteral("Drizzle canvas"),
                QStringLiteral("%1x%2 drizzled pixels from %3 light(s), %4 guarded at each edge")
                    .arg(geometry.canvas.width)
                    .arg(geometry.canvas.height)
                    .arg(static_cast<int>(geometry.frames.size()))
                    .arg(geometry.canvas.edgeGuard));

            const DrizzleFingerprints fingerprints = fingerprintsOf(geometry, group, executionPreparationSnapshot());
            std::vector<ExpectedDescriptor> expected;
            expected.reserve(geometry.frames.size());
            for (const auto& frame : geometry.frames)
            {
                an::Result<ExpectedDescriptor> wanted =
                    expectedDescriptorOf(frame, geometry.canvas, static_cast<double>(drizzle.scale));
                if (!wanted.hasValue())
                {
                    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                        wanted.status(), IntegrationErrorCode::DrizzleFailed,
                        QStringLiteral("A light's drizzle geometry could not be described"),
                        frame.frame.canonicalPath()));
                }
                expected.push_back(std::move(*wanted));
            }

            const QString directory = ImageIntegration::drizzleDescriptorDirectory(manifest, group);
            if (directory.trimmed().isEmpty())
            {
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::InvalidOutputPath,
                                     QStringLiteral("Drizzle descriptor directory is missing")));
            }
            const ImageIntegration::DrizzleDescriptorIndexRead existing =
                ImageIntegration::readDrizzleDescriptorIndex(directory, group);
            if (existing.newerSchema && !manifest.reprocessExisting)
            {
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::InvalidConfiguration,
                    QStringLiteral("The drizzle descriptors were written by a newer version and are not replaced"),
                    ImageIntegration::drizzleGroupSidecarPath(directory),
                    QStringLiteral("%1 Reprocess the group to replace them.").arg(existing.reason)));
            }
            QString reuseReason = existing.reason;
            if (!manifest.reprocessExisting && existing.index.has_value() &&
                descriptorsCurrent(*existing.index, group, geometry, fingerprints, expected, reuseReason))
            {
                reporter.logger().operationDetail(QStringLiteral("Using existing drizzle descriptors"), reuseReason);
                stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups, 1.0f,
                                                 QStringLiteral("Drizzle descriptors (reused)"), directory);
                return StageResult::success();
            }
            if (manifest.reprocessExisting)
            {
                reuseReason = QStringLiteral("Reprocess existing enabled.");
            }
            reporter.logger().operationDetail(QStringLiteral("Writing drizzle descriptors"), reuseReason);
            if (!QDir().mkpath(directory))
            {
                return StageResult::failure(IntegrationError::directoryNotWritable(directory));
            }
            (void)ImageIntegration::recoverDocumentDirectory(directory);

            const bool dense = drizzle.mapPersistencePolicy ==
                               WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint;
            const QDir root(directory);
            QSet<QString> referenced;
            QJsonArray frames;
            int written = 0;
            for (ExpectedDescriptor& wanted : expected)
            {
                if (token.isCancellationRequested() || stop.stop_requested())
                {
                    return StageResult::cancelled();
                }
                const FrameId frame(wanted.document.frameId);
                const QString base = documentBaseFor(frame);
                const QString descriptorFile = base + QLatin1String(ImageIntegration::kDrizzleDescriptorSuffix);
                if (dense)
                {
                    bool stopped = false;
                    const QByteArray samples = denseMapOf(wanted.descriptor, wanted.document.sourceWidth,
                                                          wanted.document.sourceHeight, stop, stopped);
                    if (stopped)
                    {
                        return StageResult::cancelled();
                    }
                    const auto envelope = ImageIntegration::drizzleDensePayloadEnvelope(wanted.document, samples);
                    const QByteArray bytes =
                        envelope.has_value() ? ImageIntegration::encodeDocumentEnvelope(*envelope) : QByteArray();
                    if (bytes.isEmpty())
                    {
                        return StageResult::failure(IntegrationError(IntegrationErrorCode::DrizzleFailed,
                                                                     QStringLiteral("Failed to describe a dense map"),
                                                                     wanted.document.frameId));
                    }
                    const QString denseFile = base + QLatin1String(ImageIntegration::kDrizzleDensePayloadSuffix);
                    const QString densePath = root.filePath(denseFile);
                    const auto published = ImageIntegration::publishDocumentBytes(densePath, bytes, stop);
                    if (!published.ok())
                    {
                        return publicationFailure(published, densePath);
                    }
                    wanted.document.densePayload = ImageIntegration::DrizzleDensePayloadReference{
                        denseFile, static_cast<std::uint64_t>(bytes.size()),
                        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())};
                    referenced.insert(denseFile);
                }
                const QString descriptorPath = root.filePath(descriptorFile);
                const auto published = ImageIntegration::publishDocument(
                    descriptorPath, ImageIntegration::drizzleDescriptorEnvelope(wanted.document), stop);
                if (!published.ok())
                {
                    return publicationFailure(published, descriptorPath);
                }
                referenced.insert(descriptorFile);

                QJsonObject entry;
                entry.insert(QStringLiteral("frameId"), frame.toJson());
                entry.insert(QStringLiteral("descriptorFile"), descriptorFile);
                entry.insert(QStringLiteral("mapRepresentation"),
                             dense ? QStringLiteral("DensePayload") : QStringLiteral("CompactDescriptor"));
                frames.append(entry);
                ++written;
                stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups,
                                                 static_cast<float>(written) / static_cast<float>(expected.size()),
                                                 dense ? QStringLiteral("Drizzle descriptor and dense map")
                                                       : QStringLiteral("Drizzle descriptor"),
                                                 descriptorPath);
            }

            QJsonObject sidecar;
            sidecar.insert(QStringLiteral("schemaVersion"),
                           ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion);
            ImageIntegration::schema::insertArtifactFamilyIdentity(
                sidecar, ImageIntegration::schema::ArtifactFamily::DrizzleDescriptor);
            sidecar.insert(QStringLiteral("stageId"), QString::fromLatin1(ImageIntegration::kDrizzleStageId));
            sidecar.insert(QStringLiteral("integrationGroupKey"), group.groupKey.toString());
            sidecar.insert(QStringLiteral("outputWidth"), static_cast<qint64>(geometry.canvas.width));
            sidecar.insert(QStringLiteral("outputHeight"), static_cast<qint64>(geometry.canvas.height));
            sidecar.insert(QStringLiteral("edgeGuard"), static_cast<qint64>(geometry.canvas.edgeGuard));
            sidecar.insert(QStringLiteral("finishedWidth"), static_cast<qint64>(geometry.canvas.outputWidth));
            sidecar.insert(QStringLiteral("finishedHeight"), static_cast<qint64>(geometry.canvas.outputHeight));
            sidecar.insert(QStringLiteral("analysisWidth"), static_cast<qint64>(geometry.canvas.analysisWidth));
            sidecar.insert(QStringLiteral("analysisHeight"), static_cast<qint64>(geometry.canvas.analysisHeight));
            sidecar.insert(QStringLiteral("canvasOriginX"), geometry.canvas.origin.x);
            sidecar.insert(QStringLiteral("canvasOriginY"), geometry.canvas.origin.y);
            sidecar.insert(QStringLiteral("referenceFrame"),
                           geometry.frames[geometry.referenceIndex].frame.canonicalPath());
            sidecar.insert(QStringLiteral("registrationSettingsVersion"), geometry.settingsVersion);
            sidecar.insert(QStringLiteral("scale"), static_cast<double>(drizzle.scale));
            sidecar.insert(QStringLiteral("pixelFraction"), static_cast<double>(drizzle.pixelFraction));
            sidecar.insert(QStringLiteral("kernel"), drizzle.kernel);
            sidecar.insert(QStringLiteral("cfaAware"), drizzle.cfaAware);
            sidecar.insert(QStringLiteral("cfaPattern"), drizzle.cfaPattern);
            sidecar.insert(QStringLiteral("transformsFingerprint"), fingerprints.transforms);
            sidecar.insert(QStringLiteral("sourceDistortionFingerprint"), fingerprints.sourceDistortions);
            sidecar.insert(QStringLiteral("sourceInputsFingerprint"), fingerprints.sourceInputs);
            sidecar.insert(QStringLiteral("mapPersistencePolicy"), persistencePolicyKey(drizzle.mapPersistencePolicy));
            if (!geometry.cometProductsFingerprint.isEmpty())
            {
                sidecar.insert(QStringLiteral("cometProductsFingerprint"), geometry.cometProductsFingerprint);
                sidecar.insert(QStringLiteral("cometRunId"), geometry.cometRunId);
                sidecar.insert(QStringLiteral("cometRole"), geometry.cometRole);
                sidecar.insert(QStringLiteral("cometOutputPlaneId"), geometry.cometOutputPlaneId);
            }
            if (!geometry.withoutTransform.isEmpty())
            {
                sidecar.insert(QStringLiteral("withoutTransform"),
                               QJsonArray::fromStringList(geometry.withoutTransform));
            }
            sidecar.insert(QStringLiteral("frames"), frames);
            sidecar.insert(QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            const QByteArray sidecarBytes = QJsonDocument(sidecar).toJson(QJsonDocument::Compact);
            if (sidecarBytes.size() > ImageIntegration::schema::kMetadataSidecarMaxBytes)
            {
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::FileNotWritable, QStringLiteral("Drizzle metadata exceeds the sidecar limit"),
                    ImageIntegration::drizzleGroupSidecarPath(directory)));
            }
            const QString sidecarPath = ImageIntegration::drizzleGroupSidecarPath(directory);
            const auto published = ImageIntegration::publishDocumentBytes(sidecarPath, sidecarBytes, stop);
            if (!published.ok())
            {
                return publicationFailure(published, sidecarPath);
            }

            // Descriptors and dense maps of lights the sidecar no longer lists are this stage's and are removed.
            const auto recovered = ImageIntegration::recoverDocumentDirectory(directory, [&](const QString& candidate) {
                const QString file = QFileInfo(candidate).fileName();
                return (file.endsWith(QLatin1String(ImageIntegration::kDrizzleDescriptorSuffix)) ||
                        file.endsWith(QLatin1String(ImageIntegration::kDrizzleDensePayloadSuffix))) &&
                       !referenced.contains(file);
            });
            for (const QString& failed : recovered.failed)
            {
                LOG_WARNING("LightsDrizzleStage: could not remove '{}'", failed.toStdString());
            }
            stage_utils::reportStageProgress(reporter, stageId(), groupIndex, totalGroups, 1.0f,
                                             QStringLiteral("Drizzle descriptors ready"), directory);
            return StageResult::success();
        };

        const StageResult groupResult = processGroup();
        if (groupResult.isCancelled())
        {
            return StageResult::cancelled();
        }
        if (groupResult.isFailure())
        {
            if (abortStage)
            {
                reporter.logger().warning(
                    QStringLiteral("Group '%1' failed: %2").arg(groupLabel, groupResult.error.toUserString()));
                reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
                return groupResult;
            }
            errorReport.addError(groupResult.error);
            reporter.logger().warning(QStringLiteral("Group '%1' failed: %2 - continuing with remaining groups.")
                                          .arg(groupLabel, groupResult.error.toUserString()));
        }
        reporter.logger().groupCompleted(groupIndex, totalGroups, groupLabel, groupTimer.elapsed());
    }

    reporter.logger().stageCompleted(name(), totalGroups, stageTimer.elapsed());
    if (!errorReport.errors.empty())
    {
        return StageResult::partialSuccess(errorReport);
    }
    return StageResult::success();
}
