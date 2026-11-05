// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The execution planner: what a run recomputes and what it reuses, and why.

#include "stdafx.hpp"
#include "ExecutionPlanner.h"
#include "DirtyStateTracker.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"
#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QMap>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>

namespace
{
QString canonicalOrRaw(const FrameId& id)
{
    const QString canon = id.canonicalPath();
    if (!canon.isEmpty())
    {
        return canon;
    }

    const QString raw = id.toString();
    LOG_WARNING("ExecutionPlanner: FrameId canonicalPath() empty; falling back to raw path key '{}'",
                raw.toStdString());

    return raw;
}

QString outputPathFor(const WorkflowManifest& manifest, const QString& relative_or_absolute);

QString masterPathForGroup(const WorkflowManifest& manifest, const WorkflowManifest::CalibrationStage::Group& group)
{
    if (group.existingMaster.has_value())
    {
        return canonicalOrRaw(group.existingMaster.value());
    }

    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return group.outputFilename;
    }
    return QDir(manifest.outputDirectory).filePath(group.outputFilename);
}

bool isDrizzleEnabled(const WorkflowManifest::IntegrationGroup& group)
{
    return group.drizzle.has_value() && group.drizzle->enabled;
}

bool cometNormalizationRequiresRegisteredOutputs(const WorkflowManifest::IntegrationGroup& group)
{
    if (!ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode) ||
        group.stacking.normalizationMode == NormalizationMode::None)
    {
        return false;
    }

    const bool pure_comet_local_drizzle = isDrizzleEnabled(group) &&
                                          group.stacking.normalizationMode == NormalizationMode::Local &&
                                          group.stacking.localNormalization.enabled &&
                                          ImageIntegration::usesCometOnlyStackingMode(group.stacking.cometStackingMode);
    return !pure_comet_local_drizzle;
}

bool cometAlignmentRequiresRegisteredOutputs(const WorkflowManifest::IntegrationGroup& group)
{
    if (!ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode))
    {
        return false;
    }

    return !isDrizzleEnabled(group) || cometNormalizationRequiresRegisteredOutputs(group);
}

bool alignmentProductsRequired(
    const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group,
    const ExecutionPreparationSnapshot* snapshot = nullptr,
    const std::optional<ImageIntegration::TransformOnReadRouting>& prepared_decision = std::nullopt)
{
    if (group.registrationGroupKey.toString().trimmed().isEmpty())
    {
        return false;
    }

    if (isDrizzleEnabled(group) && !cometNormalizationRequiresRegisteredOutputs(group))
    {
        return false;
    }

    const auto decision = prepared_decision.has_value()
                              ? prepared_decision.value()
                              : ImageIntegration::planLightsTransformOnRead(manifest, group, snapshot);
    return !decision.usesTransformOnRead();
}

bool normalizationArtifactMetadataLooksReusable(const WorkflowManifest& manifest,
                                                const WorkflowManifest::IntegrationGroup& group,
                                                const ExecutionPreparationSnapshot& snapshot)
{
    if (ImageIntegration::transformOnReadGlobalCoefficientsReusable(manifest, group, snapshot))
    {
        return true;
    }

    QString artifact_path = group.normalizationCoefficientsArtifactPath.value_or(QString());
    if (artifact_path.trimmed().isEmpty())
    {
        const QString group_dir_name = ImageIntegration::sanitizeFilename(group.groupKey.toString());
        artifact_path =
            QDir(manifest.outputDirectory)
                .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/%1/coefficients.json")
                              .arg(group_dir_name));
    }

    const QString meta_path = artifact_path + QStringLiteral(".meta.json");
    const auto* artifact_stamp = snapshot.fileStamp(artifact_path);
    const auto* meta_stamp = snapshot.fileStamp(meta_path);
    if (artifact_stamp == nullptr || !artifact_stamp->isFile || meta_stamp == nullptr || !meta_stamp->isFile)
    {
        return false;
    }

    if (ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode))
    {
        // Comet product fingerprints include persisted role/readiness records. Keep the plan
        // conservative until those records have been resolved by the owning stage.
        return false;
    }

    std::vector<FrameId> reference_frames = group.normalizationReferenceFrames;
    if (reference_frames.empty())
    {
        reference_frames = group.lightFiles;
        std::sort(reference_frames.begin(), reference_frames.end());
        reference_frames.erase(std::unique(reference_frames.begin(), reference_frames.end()), reference_frames.end());
        if (reference_frames.empty())
        {
            return false;
        }
        reference_frames = {reference_frames.front()};
    }

    std::vector<FrameId> effective_frames = group.lightFiles;
    std::sort(effective_frames.begin(), effective_frames.end());
    effective_frames.erase(std::unique(effective_frames.begin(), effective_frames.end()), effective_frames.end());

    if (!group.registrationGroupKey.toString().trimmed().isEmpty())
    {
        const auto reg_it = std::find_if(manifest.registrationGroups.cbegin(), manifest.registrationGroups.cend(),
                                         [&](const WorkflowManifest::RegistrationGroup& registration) {
                                             return registration.groupKey == group.registrationGroupKey;
                                         });
        if (reg_it == manifest.registrationGroups.cend())
        {
            return false;
        }

        const auto settings_version = snapshot.resolveRegistrationSettingsVersion(group.registrationGroupKey.toString(),
                                                                                  reg_it->settings.version);
        const auto* outputs =
            settings_version.has_value()
                ? snapshot.registrationMetrics(group.registrationGroupKey.toString(), *settings_version)
                : nullptr;
        if (outputs == nullptr)
        {
            return false;
        }

        for (auto& frame : effective_frames)
        {
            const auto output = outputs->constFind(frame);
            const auto* output_stamp = output != outputs->cend() ? snapshot.fileStamp(output->outputPath) : nullptr;
            if (output == outputs->cend() || !output->succeeded || output->outputPath.trimmed().isEmpty() ||
                output_stamp == nullptr || !output_stamp->isFile)
            {
                return false;
            }
            frame = FrameId(output->outputPath);
        }
    }

    const auto* metadata = snapshot.normalizationMetadata(group.groupKey);
    if (metadata == nullptr || !metadata->parsed)
        return false;
    const QJsonObject& meta = metadata->object;
    const QString expected_payload_digest = meta.value(QStringLiteral("coefficientPayloadSha256")).toString();
    return expected_payload_digest.size() == 64 &&
           ArtifactTracker::contentDigestForFile(artifact_path) == expected_payload_digest &&
           meta.value(QStringLiteral("stageId")).toString() == QStringLiteral("LightsNormalization") &&
           meta.value(QStringLiteral("integrationGroupKey")).toString() == group.groupKey.toString() &&
           meta.value(QStringLiteral("schemaVersion")).toInt() ==
               ImageIntegration::schema::kNormalizationArtifactSchemaVersion &&
           ImageIntegration::schema::statesCurrentArtifactFamily(
               meta, ImageIntegration::schema::ArtifactFamily::NormalizationCoefficients) &&
           meta.value(QStringLiteral("inputsFingerprint")).toString() ==
               computeFrameFileStampFingerprint(effective_frames, &snapshot) &&
           meta.value(QStringLiteral("settingsFingerprint")).toString() ==
               computeNormalizationSettingsFingerprint(group, reference_frames);
}

struct CurrentDrizzleFrameFacts
{
    an::RegistrationTransform transform;
    stage_utils::SourceDistortionProvenance provenance;
    std::optional<an::SipDistortion> resolvedDistortion;
    const ExecutionPreparationSnapshot::DrizzleSourceGeometryFacts* geometry = nullptr;
};

struct CurrentDrizzleMappingFacts
{
    std::unordered_map<QString, CurrentDrizzleFrameFacts> byIdentity;
    std::unordered_map<QString, an::RegistrationTransform> fingerprintTransforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> fingerprintProvenance;
    std::unordered_map<QString, std::optional<an::SipDistortion>> fingerprintResolvedDistortion;
};

[[nodiscard]] std::optional<CurrentDrizzleMappingFacts>
resolveCurrentDrizzleMappingFacts(const WorkflowManifest::IntegrationGroup& group,
                                  const ExecutionPreparationSnapshot::RegistrationMetrics& metrics,
                                  const ExecutionPreparationSnapshot& snapshot)
{
    CurrentDrizzleMappingFacts result;
    result.byIdentity.reserve(group.lightFiles.size());
    result.fingerprintTransforms.reserve(group.lightFiles.size());
    result.fingerprintProvenance.reserve(group.lightFiles.size());
    result.fingerprintResolvedDistortion.reserve(group.lightFiles.size());
    for (const auto& frame : group.lightFiles)
    {
        const auto metric = metrics.constFind(frame);
        const auto* geometry = snapshot.drizzleSourceGeometry(frame);
        if (metric == metrics.cend() || !metric->succeeded || !metric->transformParsed ||
            !metric->transform.has_value() || geometry == nullptr || !geometry->metadataProbeSucceeded)
        {
            return std::nullopt;
        }

        const stage_utils::SourceDistortionProvenance provenance = metric->sourceDistortion;
        const std::optional<an::SipDistortion> resolved_distortion =
            provenance.policy == stage_utils::SourceDistortionPolicy::InheritMetadata
                ? geometry->inheritedSourceDistortion
                : provenance.model;
        CurrentDrizzleFrameFacts frame_facts{*metric->transform, provenance, resolved_distortion, geometry};
        if (!result.byIdentity.emplace(frame.identityKey(), std::move(frame_facts)).second)
            return std::nullopt;

        const QString canonical_key = frame.canonicalPath();
        result.fingerprintTransforms.emplace(canonical_key, *metric->transform);
        result.fingerprintProvenance.emplace(canonical_key, provenance);
        result.fingerprintResolvedDistortion.emplace(canonical_key, resolved_distortion);
    }
    return result;
}

bool drizzleArtifactMetadataLooksReusable(const WorkflowManifest& manifest,
                                          const WorkflowManifest::IntegrationGroup& group,
                                          const ExecutionPreparationSnapshot& snapshot)
{
    if (!group.drizzle.has_value() || !group.drizzle->enabled)
    {
        return false;
    }
    if (ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode))
    {
        // Comet transform/readiness records are resolved by the runtime stages and are not captured in this
        // snapshot. Comet stacks were already conservatively selected because their expected stack hash is unknown.
        return false;
    }

    QString descriptor_dir = group.drizzlePixmapArtifactDir.value_or(QString());
    if (descriptor_dir.trimmed().isEmpty())
    {
        const QString group_dir_name = ImageIntegration::sanitizeFilename(group.groupKey.toString());
        descriptor_dir =
            QDir(manifest.outputDirectory)
                .filePath(
                    QStringLiteral(".afternight/image_integration/artifacts/drizzle/%1/pixmaps").arg(group_dir_name));
    }

    // The group's sidecar lists one descriptor document per light. A sidecar of another schema - one that lists map
    // files instead, which are never read - never matches.
    const auto* metadata = snapshot.drizzleMetadata(group.groupKey);
    if (metadata == nullptr || !metadata->parsed)
        return false;
    const QJsonObject& meta = metadata->object;
    const auto& drizzle = group.drizzle.value();
    if (meta.value(QStringLiteral("schemaVersion")).toInt() !=
            ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion ||
        !ImageIntegration::schema::statesCurrentArtifactFamily(
            meta, ImageIntegration::schema::ArtifactFamily::DrizzleDescriptor) ||
        meta.value(QStringLiteral("stageId")).toString() != QStringLiteral("LightsDrizzle") ||
        meta.value(QStringLiteral("integrationGroupKey")).toString() != group.groupKey.toString() ||
        ImageIntegration::RegistrationHashUtils::fingerprintChoice(ImageIntegration::PersistedChoice::DrizzleKernel,
                                                                   meta.value(QStringLiteral("kernel")).toString()) !=
            ImageIntegration::RegistrationHashUtils::fingerprintChoice(ImageIntegration::PersistedChoice::DrizzleKernel,
                                                                       drizzle.kernel) ||
        meta.value(QStringLiteral("cfaAware")).toBool() != drizzle.cfaAware ||
        meta.value(QStringLiteral("cfaPattern")).toString() != drizzle.cfaPattern ||
        std::abs(meta.value(QStringLiteral("scale")).toDouble() - static_cast<double>(drizzle.scale)) > 1e-6 ||
        std::abs(meta.value(QStringLiteral("pixelFraction")).toDouble() - static_cast<double>(drizzle.pixelFraction)) >
            1e-6 ||
        !meta.value(QStringLiteral("cometProductsFingerprint")).toString().isEmpty())
    {
        return false;
    }

    const auto reg_it = std::find_if(manifest.registrationGroups.cbegin(), manifest.registrationGroups.cend(),
                                     [&](const WorkflowManifest::RegistrationGroup& registration) {
                                         return registration.groupKey == group.registrationGroupKey;
                                     });
    if (reg_it == manifest.registrationGroups.cend())
    {
        return false;
    }

    const auto settings_version =
        snapshot.resolveRegistrationSettingsVersion(group.registrationGroupKey.toString(), reg_it->settings.version);
    const auto* metrics = settings_version.has_value()
                              ? snapshot.registrationMetrics(group.registrationGroupKey.toString(), *settings_version)
                              : nullptr;
    if (metrics == nullptr)
        return false;
    const auto current_facts = resolveCurrentDrizzleMappingFacts(group, *metrics, snapshot);
    if (!current_facts.has_value())
        return false;
    if (meta.value(QStringLiteral("transformsFingerprint")).toString() !=
            stage_utils::fingerprintTransforms(current_facts->fingerprintTransforms) ||
        meta.value(QStringLiteral("sourceInputsFingerprint")).toString() !=
            computeFrameFileStampFingerprint(group.lightFiles, &snapshot) ||
        meta.value(QStringLiteral("sourceDistortionFingerprint")).toString() !=
            stage_utils::fingerprintSourceDistortions(current_facts->fingerprintTransforms,
                                                      current_facts->fingerprintProvenance,
                                                      current_facts->fingerprintResolvedDistortion))
    {
        return false;
    }

    const int output_width = meta.value(QStringLiteral("outputWidth")).toInt();
    const int output_height = meta.value(QStringLiteral("outputHeight")).toInt();
    const bool dense_required = drizzle.mapPersistencePolicy ==
                                WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint;
    QSet<QString> described;
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    for (const auto& entry : frames)
    {
        const QJsonObject frame = entry.toObject();
        try
        {
            const FrameId frame_id = FrameId::fromJson(frame.value(QStringLiteral("frameId")));
            const QString frame_lookup_key = frame_id.identityKey();
            const QString descriptor_file = frame.value(QStringLiteral("descriptorFile")).toString();
            if (!frame_id.isValid() || described.contains(frame_lookup_key) || descriptor_file.trimmed().isEmpty())
            {
                return false;
            }
            const auto current = current_facts->byIdentity.find(frame_lookup_key);
            if (current == current_facts->byIdentity.end())
            {
                return false;
            }

            // A descriptor is a bounded document, read whole; a dense payload is only checked to be the file the
            // descriptor names, at the length it states - the stage verifies its checksum when it reads it.
            const QString descriptor_path = QDir(descriptor_dir).filePath(descriptor_file);
            const auto* descriptor_stamp = snapshot.fileStamp(descriptor_path);
            QString descriptor_error;
            const auto descriptor = descriptor_stamp != nullptr && descriptor_stamp->isFile
                                        ? ImageIntegration::readDrizzleDescriptor(descriptor_path, descriptor_error)
                                        : std::nullopt;
            if (!descriptor.has_value() || descriptor->engineIdentity.isEmpty() ||
                FrameId(descriptor->frameId).identityKey() != frame_lookup_key ||
                stage_utils::fingerprintRegistrationTransform(descriptor->transform) !=
                    stage_utils::fingerprintRegistrationTransform(current->second.transform) ||
                stage_utils::fingerprintSourceDistortionHex(descriptor->sourceDistortion) !=
                    stage_utils::fingerprintSourceDistortionHex(current->second.provenance) ||
                stage_utils::fingerprintSourceDistortionHex(current->second.provenance.policy,
                                                            descriptor->resolvedSourceDistortion) !=
                    stage_utils::fingerprintSourceDistortionHex(current->second.provenance.policy,
                                                                current->second.resolvedDistortion) ||
                descriptor->sourceWidth != current->second.geometry->sourceWidth ||
                descriptor->sourceHeight != current->second.geometry->sourceHeight ||
                static_cast<int>(descriptor->outputWidth) != output_width ||
                static_cast<int>(descriptor->outputHeight) != output_height ||
                std::abs(descriptor->scale - static_cast<double>(drizzle.scale)) > 1e-6)
            {
                return false;
            }
            if (dense_required)
            {
                if (!descriptor->densePayload.has_value())
                {
                    return false;
                }
                const auto* dense_stamp =
                    snapshot.fileStamp(QDir(descriptor_dir).filePath(descriptor->densePayload->fileName));
                if (dense_stamp == nullptr || !dense_stamp->isFile ||
                    static_cast<std::uint64_t>(dense_stamp->sizeBytes) != descriptor->densePayload->length)
                {
                    return false;
                }
            }
            described.insert(frame_lookup_key);
        }
        catch (...)
        {
            return false;
        }
    }

    for (const auto& frame : group.lightFiles)
    {
        if (!described.contains(frame.identityKey()))
        {
            return false;
        }
    }
    return described.size() == static_cast<qsizetype>(group.lightFiles.size());
}

std::optional<QString> expectedStackedArtifactHash(const WorkflowManifest& manifest,
                                                   const WorkflowManifest::IntegrationGroup& group,
                                                   const ExecutionPreparationSnapshot& snapshot)
{
    if (ImageIntegration::usesCometStackingMode(group.stacking.cometStackingMode))
    {
        return std::nullopt;
    }

    std::vector<FrameId> input_frames = group.lightFiles;
    std::sort(input_frames.begin(), input_frames.end());
    input_frames.erase(std::unique(input_frames.begin(), input_frames.end()), input_frames.end());
    if (input_frames.empty())
    {
        return std::nullopt;
    }

    const auto reg_it = std::find_if(manifest.registrationGroups.cbegin(), manifest.registrationGroups.cend(),
                                     [&](const WorkflowManifest::RegistrationGroup& registration) {
                                         return registration.groupKey == group.registrationGroupKey;
                                     });
    const bool has_registration = reg_it != manifest.registrationGroups.cend();
    const WorkflowManifest::RegistrationGroup* registration_group = has_registration ? &*reg_it : nullptr;

    std::vector<FrameId> registration_inputs;
    registration_inputs.reserve(input_frames.size());
    for (const auto& frame : input_frames)
    {
        registration_inputs.emplace_back(registrationInputPathForFrame(
            manifest.outputDirectory, manifest.calibrateLightsInMemory, registration_group, frame, &snapshot));
    }

    std::optional<int> settings_version;
    const ExecutionPreparationSnapshot::RegistrationMetrics* metrics = nullptr;
    if (has_registration)
    {
        settings_version = snapshot.resolveRegistrationSettingsVersion(group.registrationGroupKey.toString(),
                                                                       reg_it->settings.version);
        if (!settings_version.has_value())
            return std::nullopt;
        metrics = snapshot.registrationMetrics(group.registrationGroupKey.toString(), *settings_version);
        if (metrics == nullptr)
            return std::nullopt;
    }

    QString transforms_fingerprint;
    QString source_distortion_fingerprint;
    std::vector<FrameId> stack_inputs;
    if (isDrizzleEnabled(group))
    {
        if (metrics == nullptr || !settings_version.has_value())
        {
            return std::nullopt;
        }
        const auto current_facts = resolveCurrentDrizzleMappingFacts(group, *metrics, snapshot);
        if (!current_facts.has_value() || current_facts->byIdentity.size() < 2)
            return std::nullopt;

        stack_inputs = registration_inputs;
        transforms_fingerprint = stage_utils::fingerprintTransforms(current_facts->fingerprintTransforms);
        source_distortion_fingerprint = stage_utils::fingerprintSourceDistortions(
            current_facts->fingerprintTransforms, current_facts->fingerprintProvenance,
            current_facts->fingerprintResolvedDistortion);
    }
    else if (has_registration)
    {
        for (const auto& frame : input_frames)
        {
            const auto output = metrics->constFind(frame);
            const auto* output_stamp = output != metrics->cend() ? snapshot.fileStamp(output->outputPath) : nullptr;
            if (output == metrics->cend() || !output->succeeded || output->outputPath.trimmed().isEmpty() ||
                output_stamp == nullptr || !output_stamp->isFile)
            {
                return std::nullopt;
            }
            stack_inputs.emplace_back(output->outputPath);
        }
    }
    else
    {
        stack_inputs = registration_inputs;
    }

    return computeStackedLightArtifactHash(manifest, group, computeFrameFileStampFingerprint(stack_inputs, &snapshot),
                                           transforms_fingerprint, source_distortion_fingerprint, {}, &snapshot);
}

bool stackedOutputReusableForGroup(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group,
                                   const ExecutionPreparationSnapshot& snapshot)
{
    if (!group.skipIfExists || manifest.reprocessExisting)
    {
        return false;
    }

    const auto artifact_hash = expectedStackedArtifactHash(manifest, group, snapshot);
    return artifact_hash.has_value() &&
           snapshot.isArtifactValid(outputPathFor(manifest, group.outputFilename), *artifact_hash,
                                    PreparationArtifactValidation::StrictContent);
}

QSet<GroupKey> reusableRegistrationGroups(const WorkflowManifest& manifest,
                                          const ExecutionPreparationSnapshot& snapshot)
{
    QSet<GroupKey> reusable;
    if (manifest.outputDirectory.trimmed().isEmpty() || manifest.registrationGroups.empty())
    {
        return reusable;
    }

    if (!snapshot.registrationDatabaseUsable())
        return reusable;

    for (const auto& group : manifest.registrationGroups)
    {
        const RegistrationWorkAnalysis analysis = analyzeRegistrationWork(snapshot, manifest, group);
        const bool metrics_without_cached_stars =
            analysis.reasons.size() == 1 &&
            analysis.reasons.front().code == RegistrationWorkReasonCode::MissingCachedStars;
        if (!analysis.needsWork || metrics_without_cached_stars)
            reusable.insert(group.groupKey);
    }

    return reusable;
}

size_t generatedCalibrationGroupCount(const std::optional<WorkflowManifest::CalibrationStage>& stage)
{
    if (!stage.has_value())
    {
        return 0;
    }

    return static_cast<size_t>(std::count_if(
        stage->groups.begin(), stage->groups.end(),
        [](const WorkflowManifest::CalibrationStage::Group& group) { return !group.existingMaster.has_value(); }));
}

bool planSelectsAllGeneratedWork(const WorkflowManifest& manifest, const ExecutionPlan& plan)
{
    const size_t calibration_count = generatedCalibrationGroupCount(manifest.biasStage) +
                                     generatedCalibrationGroupCount(manifest.darkStage) +
                                     generatedCalibrationGroupCount(manifest.flatStage);
    const size_t selected_calibration_count =
        plan.biasGroupsToProcess.size() + plan.darkGroupsToProcess.size() + plan.flatGroupsToProcess.size();

    return selected_calibration_count >= calibration_count &&
           plan.registrationGroupsToProcess.size() >= manifest.registrationGroups.size() &&
           plan.integrationGroupsToProcess.size() >= manifest.integrationGroups.size();
}

void populateMissingArtifactsPlan(const WorkflowManifest& manifest, const QSet<GroupKey>& reusable_registration_groups,
                                  const ExecutionPreparationSnapshot& snapshot, ExecutionPlan& plan)
{
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return;
    }

    const QDir output_root(manifest.outputDirectory);
    QSet<GroupKey> bias_missing;
    QSet<GroupKey> dark_missing;
    QSet<GroupKey> flat_missing;
    QSet<GroupKey> registration_missing;
    QSet<IntegrationGroupKey> integration_missing;
    QSet<QString> stale_calibration_outputs;

    const auto markRegistrationAndDependentsMissing = [&](const GroupKey& reg_key) {
        registration_missing.insert(reg_key);
        for (const auto& group : manifest.integrationGroups)
        {
            if (group.registrationGroupKey == reg_key)
            {
                integration_missing.insert(group.groupKey);
            }
        }
    };

    const auto addCalMissing = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                   QSet<GroupKey>& out) {
        if (!stage.has_value())
        {
            return;
        }

        for (const auto& group : stage->groups)
        {
            const QString path = masterPathForGroup(manifest, group);
            const auto* stamp = snapshot.fileStamp(path);
            const bool exists = stamp != nullptr && stamp->exists;
            const bool tracked_and_stale =
                !group.existingMaster.has_value() && snapshot.hasArtifactRecord(path) &&
                !snapshot.isArtifactValid(path, computeCalibrationGroupHash(manifest, group, &snapshot),
                                          PreparationArtifactValidation::StrictContent);
            if (!exists || tracked_and_stale)
            {
                out.insert(group.groupKey);
                stale_calibration_outputs.insert(canonicalOrRaw(FrameId(path)));
            }
        }
    };

    addCalMissing(manifest.biasStage, bias_missing);
    addCalMissing(manifest.darkStage, dark_missing);
    addCalMissing(manifest.flatStage, flat_missing);

    if (!stale_calibration_outputs.isEmpty())
    {
        QSet<QString> affected_lights;
        for (const auto& params : manifest.lightsCalibration)
        {
            const auto uses_stale_master = [&](const std::optional<FrameId>& master) {
                return master.has_value() && stale_calibration_outputs.contains(canonicalOrRaw(master.value()));
            };
            if (uses_stale_master(params.biasPath) || uses_stale_master(params.darkPath) ||
                uses_stale_master(params.flatPath))
            {
                affected_lights.insert(canonicalOrRaw(params.lightPath));
            }
        }

        for (const auto& group : manifest.registrationGroups)
        {
            const bool affected =
                std::any_of(group.lightFiles.cbegin(), group.lightFiles.cend(),
                            [&](const FrameId& frame) { return affected_lights.contains(canonicalOrRaw(frame)); });
            if (affected)
            {
                markRegistrationAndDependentsMissing(group.groupKey);
            }
        }
    }

    for (const auto& group : manifest.registrationGroups)
    {
        if (!reusable_registration_groups.contains(group.groupKey))
        {
            markRegistrationAndDependentsMissing(group.groupKey);
        }
    }

    for (const auto& group : manifest.integrationGroups)
    {
        const QString stacked_path = output_root.filePath(group.outputFilename);
        const auto* stacked_stamp = snapshot.fileStamp(stacked_path);
        const bool tracked_and_stale =
            snapshot.hasArtifactRecord(stacked_path) && !stackedOutputReusableForGroup(manifest, group, snapshot);
        const bool stacked_reusable = stacked_stamp != nullptr && stacked_stamp->exists && !tracked_and_stale;
        if (!stacked_reusable)
        {
            integration_missing.insert(group.groupKey);
        }

        const bool drizzle_enabled = group.drizzle.has_value() && group.drizzle->enabled;
        if (drizzle_enabled)
        {
            if (!drizzleArtifactMetadataLooksReusable(manifest, group, snapshot))
            {
                integration_missing.insert(group.groupKey);
            }
            continue;
        }

        // Missing registered-light outputs are an alignment/integration artifact problem, not a reason to recompute
        // registration metrics when registration_metrics.db is otherwise usable.
        if (!ImageIntegration::lightsAlignmentOutputsReusable(manifest, group, snapshot))
        {
            const bool discardable_outputs_are_optional =
                stacked_reusable && group.stacking.registeredOutputPolicy == RegisteredOutputPolicy::Discardable &&
                reusable_registration_groups.contains(group.registrationGroupKey) &&
                ImageIntegration::planLightsTransformOnRead(manifest, group, nullptr).usesTransformOnRead();
            if (!discardable_outputs_are_optional)
            {
                integration_missing.insert(group.groupKey);
            }
        }
    }

    plan.biasGroupsToProcess.assign(bias_missing.begin(), bias_missing.end());
    plan.darkGroupsToProcess.assign(dark_missing.begin(), dark_missing.end());
    plan.flatGroupsToProcess.assign(flat_missing.begin(), flat_missing.end());
    plan.registrationGroupsToProcess.assign(registration_missing.begin(), registration_missing.end());
    plan.integrationGroupsToProcess.assign(integration_missing.begin(), integration_missing.end());
}

QString joinGroupKeys(const std::vector<GroupKey>& keys)
{
    QStringList items;
    items.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys)
    {
        items.append(key.toCompactString());
    }
    return items.isEmpty() ? QStringLiteral("-") : items.join(QStringLiteral(", "));
}

QString joinIntegrationKeys(const std::vector<IntegrationGroupKey>& keys)
{
    QStringList items;
    items.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys)
    {
        items.append(key.toCompactString());
    }
    return items.isEmpty() ? QStringLiteral("-") : items.join(QStringLiteral(", "));
}

using PlanAction = ExecutionPlanDecision::Action;

QString actionLabel(PlanAction action)
{
    switch (action)
    {
    case PlanAction::Recompute:
        return QStringLiteral("Recompute");
    case PlanAction::Reuse:
        return QStringLiteral("Reuse");
    case PlanAction::Skip:
        return QStringLiteral("Skip");
    }
    return QStringLiteral("Skip");
}

QString outputPathFor(const WorkflowManifest& manifest, const QString& relative_or_absolute)
{
    if (relative_or_absolute.trimmed().isEmpty())
    {
        return QStringLiteral("-");
    }

    const QFileInfo info(relative_or_absolute);
    if (info.isAbsolute() || manifest.outputDirectory.trimmed().isEmpty())
    {
        return relative_or_absolute;
    }
    return QDir(manifest.outputDirectory).filePath(relative_or_absolute);
}

QString normalizedPlanPath(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return QString();
    }

    const QFileInfo info(path);
    return QDir::cleanPath(info.isAbsolute() ? info.absoluteFilePath() : QFileInfo(path).absoluteFilePath());
}

QString frameCountText(qsizetype count)
{
    return QStringLiteral("%1 frame%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("s"));
}

int estimateFrameCost(qsizetype frame_count, double seconds_per_frame)
{
    if (frame_count <= 0 || seconds_per_frame <= 0.0)
    {
        return 0;
    }
    return (std::max)(1, static_cast<int>(std::ceil(static_cast<double>(frame_count) * seconds_per_frame)));
}

bool stageEnabled(const WorkflowManifest& manifest, const QString& stage_name)
{
    return manifest.stageSequence.isEmpty() || manifest.stageSequence.contains(stage_name);
}

bool containsGroup(const std::vector<GroupKey>& groups, const GroupKey& key)
{
    return std::find(groups.begin(), groups.end(), key) != groups.end();
}

bool containsIntegrationGroup(const std::vector<IntegrationGroupKey>& groups, const IntegrationGroupKey& key)
{
    return std::find(groups.begin(), groups.end(), key) != groups.end();
}

QString baseRecomputeReason(const ExecutionPlan& plan, const std::optional<WorkflowManifest>& previousManifest)
{
    if (plan.mode == ExecutionMode::Full)
    {
        return previousManifest.has_value() ? QStringLiteral("Full run requested")
                                            : QStringLiteral("No previous successful manifest");
    }
    if (plan.mode == ExecutionMode::RegistrationOnly)
    {
        return QStringLiteral("Registration-only execution mode");
    }
    return QStringLiteral("Incremental plan selected this group");
}

QString reasonForCalibrationChange(const ManifestDiff::CalibrationGroupChange& change)
{
    using ChangeType = ManifestDiff::CalibrationGroupChange::ChangeType;

    if (change.change == ChangeType::Added)
    {
        return QStringLiteral("New calibration group");
    }
    if (change.change == ChangeType::Removed)
    {
        return QStringLiteral("Calibration group removed");
    }

    QStringList reasons;
    if (change.inputFilesChanged)
    {
        reasons << QStringLiteral("Input frames changed");
    }
    if (change.stackingSettingsChanged)
    {
        reasons << QStringLiteral("Stacking settings changed");
    }
    if (change.outputFilenameChanged)
    {
        reasons << QStringLiteral("Output filename changed");
    }
    if (change.calibrationDependenciesChanged)
    {
        reasons << QStringLiteral("Calibration dependencies changed");
    }
    if (change.calibrationFingerprintChanged)
    {
        reasons << QStringLiteral("Input or dependency file stamps changed");
    }
    return reasons.isEmpty() ? QStringLiteral("Calibration manifest changed") : reasons.join(QStringLiteral("; "));
}

QString reasonForRegistrationChange(const ManifestDiff::RegistrationGroupChange& change)
{
    using ChangeType = ManifestDiff::CalibrationGroupChange::ChangeType;

    if (change.change == ChangeType::Added)
    {
        return QStringLiteral("New registration group");
    }
    if (change.change == ChangeType::Removed)
    {
        return QStringLiteral("Registration group removed");
    }

    QStringList reasons;
    if (change.lightFilesChanged)
    {
        reasons << QStringLiteral("Light frames changed");
    }
    if (change.settingsChanged)
    {
        reasons << QStringLiteral("Registration or plate-solving settings changed");
    }
    if (change.referenceFrameChanged)
    {
        reasons << QStringLiteral("Reference frame changed");
    }
    return reasons.isEmpty() ? QStringLiteral("Registration manifest changed") : reasons.join(QStringLiteral("; "));
}

QString reasonForIntegrationChange(const ManifestDiff::IntegrationGroupChange& change)
{
    using ChangeType = ManifestDiff::CalibrationGroupChange::ChangeType;

    if (change.change == ChangeType::Added)
    {
        return QStringLiteral("New integration group");
    }
    if (change.change == ChangeType::Removed)
    {
        return QStringLiteral("Integration group removed");
    }

    QStringList reasons;
    if (change.lightFilesChanged)
    {
        reasons << QStringLiteral("Light frames changed");
    }
    if (change.stackingSettingsChanged)
    {
        reasons << QStringLiteral("Stacking, normalization, or drizzle settings changed");
    }
    if (change.calibrationAssignmentsChanged)
    {
        reasons << QStringLiteral("Calibration or registration assignment changed");
    }
    if (change.normalizationSettingsChanged)
    {
        reasons << QStringLiteral("Normalization reference frames changed");
    }
    return reasons.isEmpty() ? QStringLiteral("Integration manifest changed") : reasons.join(QStringLiteral("; "));
}

struct DiffReasonMaps
{
    QMap<QString, QString> calibration;
    QMap<QString, QString> registration;
    QMap<QString, QString> integration;
};

DiffReasonMaps buildDiffReasonMaps(const WorkflowManifest& manifest,
                                   const std::optional<WorkflowManifest>& previousManifest)
{
    DiffReasonMaps maps;
    if (!previousManifest.has_value())
    {
        return maps;
    }

    const ManifestDiff diff = compareManifests(*previousManifest, manifest);
    for (const auto& change : diff.calibrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
        {
            continue;
        }
        maps.calibration.insert(QStringLiteral("%1|%2").arg(change.frameType, change.groupKey.toString()),
                                reasonForCalibrationChange(change));
    }
    for (const auto& change : diff.registrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
        {
            continue;
        }
        maps.registration.insert(change.groupKey.toString(), reasonForRegistrationChange(change));
    }
    for (const auto& change : diff.integrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
        {
            continue;
        }
        maps.integration.insert(change.groupKey.toString(), reasonForIntegrationChange(change));
    }
    return maps;
}

ExecutionPlanDecision makeDecision(const QString& stage, const QString& groupKey, PlanAction action,
                                   const QString& reason, const QString& dependencies, int estimatedSeconds,
                                   const QString& outputs)
{
    ExecutionPlanDecision decision;
    decision.stage = stage;
    decision.groupKey = groupKey;
    decision.action = action;
    decision.actionLabel = actionLabel(action);
    decision.reason = reason;
    decision.dependencies = dependencies;
    decision.estimatedSeconds = (action == PlanAction::Recompute) ? estimatedSeconds : 0;
    decision.estimatedTime = WorkflowTimeEstimator::formatDuration(decision.estimatedSeconds);
    decision.outputs = outputs.trimmed().isEmpty() ? QStringLiteral("-") : outputs;
    return decision;
}

PlanAction executionActionFor(const ExecutionPlan& plan, bool selectedForIncremental, bool skipIfExistingOutput,
                              const QString& outputPath, const ExecutionPreparationSnapshot* snapshot = nullptr)
{
    if (plan.mode == ExecutionMode::Incremental)
    {
        return selectedForIncremental ? PlanAction::Recompute : PlanAction::Reuse;
    }

    const auto* output_stamp = snapshot != nullptr ? snapshot->fileStamp(outputPath) : nullptr;
    const bool output_exists =
        snapshot != nullptr ? output_stamp != nullptr && output_stamp->exists : QFileInfo::exists(outputPath);
    if (skipIfExistingOutput && !outputPath.trimmed().isEmpty() && output_exists)
    {
        return PlanAction::Reuse;
    }

    return PlanAction::Recompute;
}

struct AlignmentPlanStatus
{
    bool required = false;
    PlanAction action = PlanAction::Skip;
    std::optional<ImageIntegration::TransformOnReadRouting> transformOnReadDecision;
};

AlignmentPlanStatus alignmentPlanStatusFor(const WorkflowManifest& manifest, const ExecutionPlan& plan,
                                           const WorkflowManifest::IntegrationGroup& group,
                                           const ExecutionPreparationSnapshot* snapshot = nullptr)
{
    AlignmentPlanStatus status;
    if (!stageEnabled(manifest, QStringLiteral("LightsAlignment")))
    {
        return status;
    }

    if (!group.registrationGroupKey.toString().trimmed().isEmpty() &&
        (!isDrizzleEnabled(group) || cometNormalizationRequiresRegisteredOutputs(group)))
    {
        status.transformOnReadDecision = ImageIntegration::planLightsTransformOnRead(manifest, group, snapshot);
    }
    status.required = alignmentProductsRequired(manifest, group, snapshot, status.transformOnReadDecision);
    if (!status.required)
    {
        return status;
    }

    const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
    const bool registration_selected = containsGroup(plan.registrationGroupsToProcess, group.registrationGroupKey);
    const bool missing_registered_output =
        snapshot != nullptr ? !ImageIntegration::lightsAlignmentOutputsReusable(manifest, group, *snapshot)
                            : !ImageIntegration::lightsAlignmentOutputsReusable(manifest, group);
    const bool should_recompute =
        plan.mode != ExecutionMode::Incremental || registration_selected ||
        (selected && (missing_registered_output || cometAlignmentRequiresRegisteredOutputs(group)));
    status.action = should_recompute ? PlanAction::Recompute : PlanAction::Reuse;
    return status;
}

QString reasonForAction(const ExecutionPlan& plan, PlanAction action, const QString& dirtyReason,
                        const QString& reuseReason, const std::optional<WorkflowManifest>& previousManifest)
{
    if (action == PlanAction::Reuse)
    {
        return reuseReason;
    }
    if (!dirtyReason.trimmed().isEmpty())
    {
        return dirtyReason;
    }
    return baseRecomputeReason(plan, previousManifest);
}

} // namespace

bool ExecutionPlan::isEmpty() const
{
    // Only meaningful for Incremental mode; other modes rely on executor stage filtering.
    if (mode != ExecutionMode::Incremental)
    {
        return false;
    }

    return biasGroupsToProcess.empty() && darkGroupsToProcess.empty() && flatGroupsToProcess.empty() &&
           registrationGroupsToProcess.empty() && integrationGroupsToProcess.empty();
}

QString ExecutionPlan::toSummaryText() const
{
    switch (mode)
    {
    case ExecutionMode::Full:
        return QStringLiteral("Full re-run");
    case ExecutionMode::RegistrationOnly:
        return QStringLiteral("Registration only, with the calibration its lights need");
    case ExecutionMode::Incremental:
        break;
    }

    if (isEmpty())
    {
        return QStringLiteral("Nothing to process");
    }

    return QStringLiteral("Incremental run: %1 bias, %2 dark, %3 flat, %4 registration, %5 integration groups")
        .arg(biasGroupsToProcess.size())
        .arg(darkGroupsToProcess.size())
        .arg(flatGroupsToProcess.size())
        .arg(registrationGroupsToProcess.size())
        .arg(integrationGroupsToProcess.size());
}

void ExecutionPlan::ensureIntegrationGroupsToProcess(const std::vector<IntegrationGroupKey>& groups)
{
    for (const auto& group : groups)
    {
        const auto it = std::find(integrationGroupsToProcess.begin(), integrationGroupsToProcess.end(), group);
        if (it == integrationGroupsToProcess.end())
        {
            integrationGroupsToProcess.push_back(group);
        }
    }
}

ExecutionPlanner::ExecutionPlanner(const IntegrationDataModel& model)
{
    (void)model;
}

ExecutionPlan ExecutionPlanner::buildPlan(const WorkflowManifest& current,
                                          const std::optional<WorkflowManifest>& previous) const
{
    const auto snapshot = ExecutionPreparationSnapshot::build(current, previous);
    return buildPlan(current, *snapshot);
}

ExecutionPlan ExecutionPlanner::buildPlan(const WorkflowManifest& current,
                                          const ExecutionPreparationSnapshot& snapshot) const
{
    const auto& previous = snapshot.previousManifest();
    if (current.reprocessExisting)
    {
        ExecutionPlan plan;
        plan.mode = ExecutionMode::Full;
        LOG_INFO("ExecutionPlanner: reprocess-existing is enabled, forcing full execution.");
        return plan;
    }

    const QSet<GroupKey> reusable_registration_groups = reusableRegistrationGroups(current, snapshot);

    if (!previous.has_value())
    {
        ExecutionPlan artifact_plan;
        artifact_plan.mode = ExecutionMode::Incremental;
        populateMissingArtifactsPlan(current, reusable_registration_groups, snapshot, artifact_plan);
        if (artifact_plan.isEmpty() || !planSelectsAllGeneratedWork(current, artifact_plan))
        {
            LOG_INFO("ExecutionPlanner: no exact previous manifest found, using artifact-backed incremental plan. {}",
                     artifact_plan.toSummaryText().toStdString());
            LOG_INFO("ExecutionPlanner: artifact-backed groups: bias=[{}], dark=[{}], flat=[{}], registration=[{}], "
                     "integration=[{}]",
                     joinGroupKeys(artifact_plan.biasGroupsToProcess).toStdString(),
                     joinGroupKeys(artifact_plan.darkGroupsToProcess).toStdString(),
                     joinGroupKeys(artifact_plan.flatGroupsToProcess).toStdString(),
                     joinGroupKeys(artifact_plan.registrationGroupsToProcess).toStdString(),
                     joinIntegrationKeys(artifact_plan.integrationGroupsToProcess).toStdString());
            return artifact_plan;
        }

        ExecutionPlan plan;
        plan.mode = ExecutionMode::Full;
        LOG_INFO("ExecutionPlanner: no previous manifest found, forcing full execution.");
        return plan;
    }

    const ManifestDiff diff = compareManifests(*previous, current);
    if (!diff.hasChanges())
    {
        ExecutionPlan plan;
        plan.mode = ExecutionMode::Incremental;

        // Even if no settings/files changed since the last successful run, users may have deleted outputs
        // (stacked images, masters, registered_lights, registration DB). In that case, the incremental
        // workflow should run and regenerate missing artifacts instead of claiming "Nothing to process".
        populateMissingArtifactsPlan(current, reusable_registration_groups, snapshot, plan);
        LOG_INFO("ExecutionPlanner: no manifest changes detected. {}", plan.toSummaryText().toStdString());
        LOG_INFO("ExecutionPlanner: missing-artifact groups: bias=[{}], dark=[{}], flat=[{}], registration=[{}], "
                 "integration=[{}]",
                 joinGroupKeys(plan.biasGroupsToProcess).toStdString(),
                 joinGroupKeys(plan.darkGroupsToProcess).toStdString(),
                 joinGroupKeys(plan.flatGroupsToProcess).toStdString(),
                 joinGroupKeys(plan.registrationGroupsToProcess).toStdString(),
                 joinIntegrationKeys(plan.integrationGroupsToProcess).toStdString());
        return plan;
    }

    DirtyStateTracker dirty;

    // Build calibration master path lookups for targeted propagation.
    QSet<QString> bias_master_paths;
    QSet<QString> dark_master_paths;
    QSet<QString> flat_master_paths;
    if (current.biasStage.has_value())
    {
        for (const auto& group : current.biasStage->groups)
        {
            bias_master_paths.insert(masterPathForGroup(current, group));
        }
    }
    if (current.darkStage.has_value())
    {
        for (const auto& group : current.darkStage->groups)
        {
            dark_master_paths.insert(masterPathForGroup(current, group));
        }
    }
    if (current.flatStage.has_value())
    {
        for (const auto& group : current.flatStage->groups)
        {
            flat_master_paths.insert(masterPathForGroup(current, group));
        }
    }

    QSet<QString> affected_lights;

    // Process calibration changes
    for (const auto& change : diff.calibrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
            continue;

        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Removed)
        {
            if (previous.has_value())
            {
                std::optional<QString> removed_master_path;
                if (change.frameType == QStringLiteral("BIAS") && previous->biasStage.has_value())
                {
                    auto it = std::find_if(previous->biasStage->groups.begin(), previous->biasStage->groups.end(),
                                           [&](const auto& g) { return g.groupKey == change.groupKey; });
                    if (it != previous->biasStage->groups.end())
                        removed_master_path = masterPathForGroup(*previous, *it);
                }
                else if (change.frameType == QStringLiteral("DARK") && previous->darkStage.has_value())
                {
                    auto it = std::find_if(previous->darkStage->groups.begin(), previous->darkStage->groups.end(),
                                           [&](const auto& g) { return g.groupKey == change.groupKey; });
                    if (it != previous->darkStage->groups.end())
                        removed_master_path = masterPathForGroup(*previous, *it);
                }
                else if (change.frameType == QStringLiteral("FLAT") && previous->flatStage.has_value())
                {
                    auto it = std::find_if(previous->flatStage->groups.begin(), previous->flatStage->groups.end(),
                                           [&](const auto& g) { return g.groupKey == change.groupKey; });
                    if (it != previous->flatStage->groups.end())
                        removed_master_path = masterPathForGroup(*previous, *it);
                }

                if (removed_master_path.has_value())
                {
                    const QString removed_master = removed_master_path.value();
                    for (const auto& params : current.lightsCalibration)
                    {
                        if (change.frameType == QStringLiteral("BIAS") && params.biasPath.has_value() &&
                            canonicalOrRaw(params.biasPath.value()) == removed_master)
                        {
                            affected_lights.insert(canonicalOrRaw(params.lightPath));
                        }
                        else if (change.frameType == QStringLiteral("DARK") && params.darkPath.has_value() &&
                                 canonicalOrRaw(params.darkPath.value()) == removed_master)
                        {
                            affected_lights.insert(canonicalOrRaw(params.lightPath));
                        }
                        else if (change.frameType == QStringLiteral("FLAT") && params.flatPath.has_value() &&
                                 canonicalOrRaw(params.flatPath.value()) == removed_master)
                        {
                            affected_lights.insert(canonicalOrRaw(params.lightPath));
                        }
                    }
                }
            }

            continue;
        }

        DirtyReason reason = DirtyReason::StackingSettingsChanged;
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Added)
            reason = DirtyReason::Initial;
        else if (change.inputFilesChanged)
            reason = DirtyReason::GroupingOptionsChanged;

        // Only mark groups that still exist in the current manifest.
        if (change.frameType == QStringLiteral("BIAS"))
        {
            if (current.biasStage.has_value())
            {
                auto it = std::find_if(current.biasStage->groups.begin(), current.biasStage->groups.end(),
                                       [&](const auto& g) { return g.groupKey == change.groupKey; });
                if (it != current.biasStage->groups.end())
                    dirty.markBiasGroupDirty(change.groupKey, reason);
            }
        }
        else if (change.frameType == QStringLiteral("DARK"))
        {
            if (current.darkStage.has_value())
            {
                auto it = std::find_if(current.darkStage->groups.begin(), current.darkStage->groups.end(),
                                       [&](const auto& g) { return g.groupKey == change.groupKey; });
                if (it != current.darkStage->groups.end())
                    dirty.markDarkGroupDirty(change.groupKey, reason);
            }
        }
        else if (change.frameType == QStringLiteral("FLAT"))
        {
            if (current.flatStage.has_value())
            {
                auto it = std::find_if(current.flatStage->groups.begin(), current.flatStage->groups.end(),
                                       [&](const auto& g) { return g.groupKey == change.groupKey; });
                if (it != current.flatStage->groups.end())
                    dirty.markFlatGroupDirty(change.groupKey, reason);
            }
        }
    }

    // Determine which lights are affected by calibration master changes.
    QSet<QString> dirty_bias_master_paths;
    QSet<QString> dirty_dark_master_paths;
    QSet<QString> dirty_flat_master_paths;

    if (current.biasStage.has_value())
    {
        for (const auto& key : dirty.dirtyBiasGroups())
        {
            auto it = std::find_if(current.biasStage->groups.begin(), current.biasStage->groups.end(),
                                   [&](const auto& g) { return g.groupKey == key; });
            if (it != current.biasStage->groups.end())
                dirty_bias_master_paths.insert(masterPathForGroup(current, *it));
        }
    }
    if (current.darkStage.has_value())
    {
        for (const auto& key : dirty.dirtyDarkGroups())
        {
            auto it = std::find_if(current.darkStage->groups.begin(), current.darkStage->groups.end(),
                                   [&](const auto& g) { return g.groupKey == key; });
            if (it != current.darkStage->groups.end())
                dirty_dark_master_paths.insert(masterPathForGroup(current, *it));
        }
    }
    if (current.flatStage.has_value())
    {
        for (const auto& key : dirty.dirtyFlatGroups())
        {
            auto it = std::find_if(current.flatStage->groups.begin(), current.flatStage->groups.end(),
                                   [&](const auto& g) { return g.groupKey == key; });
            if (it != current.flatStage->groups.end())
                dirty_flat_master_paths.insert(masterPathForGroup(current, *it));
        }
    }

    // 1) Lights whose explicit calibration assignment changed.
    for (const auto& entry : diff.lightsCalibrationChanges)
    {
        if (!entry.changed)
            continue;
        affected_lights.insert(canonicalOrRaw(entry.lightPath));
    }

    // 2) Lights that reference a master whose content is going to be regenerated.
    for (const auto& params : current.lightsCalibration)
    {
        if (params.biasPath.has_value() && dirty_bias_master_paths.contains(canonicalOrRaw(params.biasPath.value())))
        {
            affected_lights.insert(canonicalOrRaw(params.lightPath));
            continue;
        }
        if (params.darkPath.has_value() && dirty_dark_master_paths.contains(canonicalOrRaw(params.darkPath.value())))
        {
            affected_lights.insert(canonicalOrRaw(params.lightPath));
            continue;
        }
        if (params.flatPath.has_value() && dirty_flat_master_paths.contains(canonicalOrRaw(params.flatPath.value())))
        {
            affected_lights.insert(canonicalOrRaw(params.lightPath));
            continue;
        }
    }

    // Propagate affected lights to registration + integration groups.
    for (const auto& reg : current.registrationGroups)
    {
        bool group_affected = false;
        for (const auto& light : reg.lightFiles)
        {
            if (affected_lights.contains(canonicalOrRaw(light)))
            {
                group_affected = true;
                break;
            }
        }
        if (group_affected)
        {
            dirty.markRegistrationGroupDirty(reg.groupKey, DirtyReason::CalibrationChanged,
                                             QStringLiteral("A light's calibration inputs changed"));
        }
    }

    for (const auto& ig : current.integrationGroups)
    {
        bool group_affected = false;
        for (const auto& light : ig.lightFiles)
        {
            if (affected_lights.contains(canonicalOrRaw(light)))
            {
                group_affected = true;
                break;
            }
        }
        if (group_affected)
        {
            dirty.markIntegrationGroupDirty(ig.groupKey, DirtyReason::CalibrationChanged,
                                            QStringLiteral("A light's calibration inputs changed"));
        }
    }

    // Process registration changes
    for (const auto& change : diff.registrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
            continue;

        if (!dirty.isRegistrationGroupDirty(change.groupKey) && reusable_registration_groups.contains(change.groupKey))
        {
            LOG_INFO("ExecutionPlanner: reusing completed registration checkpoint for group '{}' despite stale "
                     "successful manifest",
                     change.groupKey.toCompactString().toStdString());
            continue;
        }

        DirtyReason reason = DirtyReason::RegistrationSettingsChanged;
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Added)
            reason = DirtyReason::Initial;
        else if (change.lightFilesChanged)
            reason = DirtyReason::GroupingOptionsChanged;
        else if (change.referenceFrameChanged)
            reason = DirtyReason::GroupingOptionsChanged;

        dirty.markRegistrationGroupDirty(change.groupKey, reason);
    }

    // Propagate registration dirty state to the integration groups that depend on it.
    {
        const auto reg_dirty = dirty.dirtyRegistrationGroups();
        for (const auto& reg_key : reg_dirty)
        {
            std::vector<IntegrationGroupKey> dependent;
            for (const auto& ig : current.integrationGroups)
            {
                if (ig.registrationGroupKey == reg_key)
                {
                    dependent.push_back(ig.groupKey);
                }
            }
            if (!dependent.empty())
            {
                dirty.propagateRegistrationDirtyToIntegration(reg_key, dependent);
            }
        }
    }

    // Process integration changes
    for (const auto& change : diff.integrationChanges)
    {
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged)
            continue;

        DirtyReason reason = DirtyReason::StackingSettingsChanged;
        if (change.change == ManifestDiff::CalibrationGroupChange::ChangeType::Added)
            reason = DirtyReason::Initial;
        else if (change.lightFilesChanged)
            reason = DirtyReason::GroupingOptionsChanged;
        else if (change.calibrationAssignmentsChanged)
            reason = DirtyReason::CalibrationChanged;

        dirty.markIntegrationGroupDirty(change.groupKey, reason);
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.biasGroupsToProcess = dirty.dirtyBiasGroups();
    plan.darkGroupsToProcess = dirty.dirtyDarkGroups();
    plan.flatGroupsToProcess = dirty.dirtyFlatGroups();
    plan.registrationGroupsToProcess = dirty.dirtyRegistrationGroups();

    auto intGroups = dirty.dirtyIntegrationGroups();
    plan.integrationGroupsToProcess.reserve(intGroups.size());
    for (const auto& k : intGroups)
        plan.integrationGroupsToProcess.push_back(k);

    ExecutionPlan artifact_plan;
    artifact_plan.mode = ExecutionMode::Incremental;
    populateMissingArtifactsPlan(current, reusable_registration_groups, snapshot, artifact_plan);
    const auto append_unique = [](auto& destination, const auto& additions) {
        for (const auto& item : additions)
        {
            if (std::find(destination.cbegin(), destination.cend(), item) == destination.cend())
            {
                destination.push_back(item);
            }
        }
    };
    append_unique(plan.biasGroupsToProcess, artifact_plan.biasGroupsToProcess);
    append_unique(plan.darkGroupsToProcess, artifact_plan.darkGroupsToProcess);
    append_unique(plan.flatGroupsToProcess, artifact_plan.flatGroupsToProcess);
    append_unique(plan.registrationGroupsToProcess, artifact_plan.registrationGroupsToProcess);
    append_unique(plan.integrationGroupsToProcess, artifact_plan.integrationGroupsToProcess);

    LOG_INFO("ExecutionPlanner: manifest changes detected. {}", plan.toSummaryText().toStdString());
    LOG_INFO("ExecutionPlanner: dirty groups: bias=[{}], dark=[{}], flat=[{}], registration=[{}], integration=[{}]",
             joinGroupKeys(plan.biasGroupsToProcess).toStdString(),
             joinGroupKeys(plan.darkGroupsToProcess).toStdString(),
             joinGroupKeys(plan.flatGroupsToProcess).toStdString(),
             joinGroupKeys(plan.registrationGroupsToProcess).toStdString(),
             joinIntegrationKeys(plan.integrationGroupsToProcess).toStdString());

    return plan;
}

std::vector<ExecutionPlanDecision> buildExecutionPlanDecisions(const WorkflowManifest& manifest,
                                                               const ExecutionPlan& plan,
                                                               const std::optional<WorkflowManifest>& previousManifest)
{
    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, previousManifest);
    return buildExecutionPlanDecisions(manifest, plan, *snapshot);
}

std::vector<ExecutionPlanDecision> buildExecutionPlanDecisions(const WorkflowManifest& manifest,
                                                               const ExecutionPlan& plan,
                                                               const ExecutionPreparationSnapshot& snapshot)
{
    const auto& previousManifest = snapshot.previousManifest();
    std::vector<ExecutionPlanDecision> decisions;
    const DiffReasonMaps reasonMaps = buildDiffReasonMaps(manifest, previousManifest);
    QMap<QString, PlanAction> calibrationActionByOutputPath;
    QMap<QString, PlanAction> lightsCalibrationActionByIntegrationGroup;
    QMap<QString, PlanAction> registrationActionByGroup;
    QMap<QString, PlanAction> normalizationActionByIntegrationGroup;
    QMap<QString, PlanAction> drizzleActionByIntegrationGroup;
    const auto calibrationDependencyRecomputes = [&](const std::optional<FrameId>& master) {
        if (!master.has_value())
        {
            return false;
        }

        return calibrationActionByOutputPath.value(normalizedPlanPath(canonicalOrRaw(master.value())),
                                                   PlanAction::Skip) == PlanAction::Recompute;
    };

    const auto appendCalibrationStage = [&](const QString& stageName, const QString& frameType,
                                            const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                            const std::vector<GroupKey>& selectedGroups) {
        if (!stageEnabled(manifest, stageName) || !stage.has_value())
        {
            return;
        }

        decisions.reserve(decisions.size() + stage->groups.size());
        for (const auto& group : stage->groups)
        {
            const QString groupKey = group.groupKey.toCompactString();
            const bool externalMaster = group.existingMaster.has_value();
            const QString outputPath = externalMaster ? canonicalOrRaw(group.existingMaster.value())
                                                      : outputPathFor(manifest, group.outputFilename);
            PlanAction action = PlanAction::Skip;
            QString reason = QStringLiteral("External master selected");
            int estimate = 0;
            if (!externalMaster)
            {
                const bool selected = containsGroup(selectedGroups, group.groupKey);
                const bool dependencyRecomputes = calibrationDependencyRecomputes(group.biasMaster) ||
                                                  calibrationDependencyRecomputes(group.darkMaster);
                const bool artifactReusable =
                    selected && !dependencyRecomputes &&
                    snapshot.isArtifactValid(outputPath, computeCalibrationGroupHash(manifest, group, &snapshot),
                                             PreparationArtifactValidation::StrictContent);
                action = dependencyRecomputes
                             ? PlanAction::Recompute
                             : (artifactReusable
                                    ? PlanAction::Reuse
                                    : executionActionFor(plan, selected, group.skipIfExists, outputPath, &snapshot));
                const QString dirtyReason =
                    reasonMaps.calibration.value(QStringLiteral("%1|%2").arg(frameType, group.groupKey.toString()));
                reason = artifactReusable
                             ? QStringLiteral("Persisted master matches current inputs, dependencies, and settings")
                             : (dependencyRecomputes && dirtyReason.trimmed().isEmpty()
                                    ? QStringLiteral("Calibration dependency will be recomputed")
                                    : reasonForAction(plan, action, dirtyReason,
                                                      QStringLiteral("No calibration changes detected for this group"),
                                                      previousManifest));
                estimate = estimateFrameCost(static_cast<qsizetype>(group.inputFiles.size()), 0.05);
            }

            QStringList dependencies;
            dependencies << frameCountText(static_cast<qsizetype>(group.inputFiles.size()));
            if (group.biasMaster.has_value())
            {
                dependencies
                    << QStringLiteral("Bias: %1").arg(QFileInfo(canonicalOrRaw(group.biasMaster.value())).fileName());
            }
            if (group.darkMaster.has_value())
            {
                dependencies
                    << QStringLiteral("Dark: %1").arg(QFileInfo(canonicalOrRaw(group.darkMaster.value())).fileName());
            }

            decisions.push_back(makeDecision(stageName, groupKey, action, reason,
                                             dependencies.join(QStringLiteral("; ")), estimate, outputPath));
            if (!externalMaster)
            {
                calibrationActionByOutputPath.insert(normalizedPlanPath(outputPath), action);
            }
        }
    };

    appendCalibrationStage(QStringLiteral("Bias"), QStringLiteral("BIAS"), manifest.biasStage,
                           plan.biasGroupsToProcess);
    appendCalibrationStage(QStringLiteral("Dark"), QStringLiteral("DARK"), manifest.darkStage,
                           plan.darkGroupsToProcess);
    appendCalibrationStage(QStringLiteral("Flat"), QStringLiteral("FLAT"), manifest.flatStage,
                           plan.flatGroupsToProcess);

    if (!manifest.lightsCalibration.empty())
    {
        for (const auto& group : manifest.integrationGroups)
        {
            const auto* calibration_facts = snapshot.calibrationSubsetFacts(group.lightFiles);
            if (calibration_facts == nullptr || calibration_facts->matchingAssignmentCount == 0)
            {
                continue;
            }
            const qsizetype calibrationCount = static_cast<qsizetype>(calibration_facts->matchingAssignmentCount);

            const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
            const bool dependencyRecomputes =
                std::any_of(calibration_facts->dependencyPaths.cbegin(), calibration_facts->dependencyPaths.cend(),
                            [&](const QString& dependency_path) {
                                return calibrationActionByOutputPath.value(normalizedPlanPath(dependency_path),
                                                                           PlanAction::Skip) == PlanAction::Recompute;
                            });
            const bool stageWillRun =
                stageEnabled(manifest, QStringLiteral("LightsCalibration")) && !manifest.calibrateLightsInMemory;
            // Lights with no master and no pedestal have nothing to calibrate them with: the stage uses them as they
            // are and makes no calibrated copy, so there is nothing to reuse or make.
            if (stageWillRun && calibration_facts->calibratingAssignmentCount == 0)
            {
                decisions.push_back(makeDecision(
                    QStringLiteral("Lights Calibration"), group.groupKey.toCompactString(), PlanAction::Skip,
                    QStringLiteral("Nothing to calibrate with: no master or pedestal is assigned, so the lights are "
                                   "used as they are"),
                    frameCountText(calibrationCount), estimateFrameCost(0, 0.10),
                    QDir(manifest.outputDirectory).filePath(QStringLiteral("calibrated_lights"))));
                lightsCalibrationActionByIntegrationGroup.insert(group.groupKey.toString(), PlanAction::Skip);
                continue;
            }
            const bool artifactsReusable =
                stageWillRun && selected && !dependencyRecomputes && calibration_facts->allCalibratedArtifactsReusable;
            const PlanAction action =
                stageWillRun ? (dependencyRecomputes
                                    ? PlanAction::Recompute
                                    : (artifactsReusable ? PlanAction::Reuse
                                                         : executionActionFor(plan, selected, false, QString())))
                             : PlanAction::Skip;
            QString reason =
                stageWillRun
                    ? (artifactsReusable
                           ? QStringLiteral("Persisted calibrated light frames match current inputs and settings")
                           : (dependencyRecomputes
                                  ? QStringLiteral("Calibration dependency will be recomputed")
                                  : reasonForAction(plan, action,
                                                    reasonMaps.integration.value(group.groupKey.toString()),
                                                    QStringLiteral("Light calibration assignments are unchanged"),
                                                    previousManifest)))
                    : QStringLiteral("Light calibration will be performed in memory");
            const QString outputs = manifest.calibrateLightsInMemory
                                        ? QStringLiteral("In-memory calibrated light frames")
                                        : QDir(manifest.outputDirectory).filePath(QStringLiteral("calibrated_lights"));

            decisions.push_back(makeDecision(QStringLiteral("Lights Calibration"), group.groupKey.toCompactString(),
                                             action, reason, frameCountText(calibrationCount),
                                             estimateFrameCost(calibrationCount, 0.10), outputs));
            lightsCalibrationActionByIntegrationGroup.insert(group.groupKey.toString(), action);
        }
    }

    if (stageEnabled(manifest, QStringLiteral("LightsRegistration")))
    {
        decisions.reserve(decisions.size() + manifest.registrationGroups.size());
        for (const auto& group : manifest.registrationGroups)
        {
            const bool selected = containsGroup(plan.registrationGroupsToProcess, group.groupKey);
            const PlanAction action = executionActionFor(plan, selected, false, QString());
            const QString reason =
                reasonForAction(plan, action, reasonMaps.registration.value(group.groupKey.toString()),
                                QStringLiteral("Registration metrics are up to date"), previousManifest);
            QStringList dependencies;
            dependencies << frameCountText(static_cast<qsizetype>(group.lightFiles.size()));
            if (group.referenceFrame.has_value())
            {
                dependencies << QStringLiteral("Reference: %1")
                                    .arg(QFileInfo(canonicalOrRaw(group.referenceFrame.value())).fileName());
            }
            const QString outputs = QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
            decisions.push_back(makeDecision(QStringLiteral("Lights Registration"), group.groupKey.toCompactString(),
                                             action, reason, dependencies.join(QStringLiteral("; ")),
                                             estimateFrameCost(static_cast<qsizetype>(group.lightFiles.size()), 0.50),
                                             outputs));
            registrationActionByGroup.insert(group.groupKey.toString(), action);
        }
    }

    const auto registrationDependencyRecomputes = [&](const WorkflowManifest::IntegrationGroup& group) {
        return registrationActionByGroup.value(group.registrationGroupKey.toString(), PlanAction::Skip) ==
               PlanAction::Recompute;
    };

    const auto lightsCalibrationDependencyRecomputes = [&](const WorkflowManifest::IntegrationGroup& group) {
        return lightsCalibrationActionByIntegrationGroup.value(group.groupKey.toString(), PlanAction::Skip) ==
               PlanAction::Recompute;
    };

    const auto integrationDependencyRecomputes = [&](const WorkflowManifest::IntegrationGroup& group,
                                                     bool includeRegistrationDependency) {
        return lightsCalibrationDependencyRecomputes(group) ||
               (includeRegistrationDependency && registrationDependencyRecomputes(group));
    };

    const auto integrationDependencyReason = [&](const WorkflowManifest::IntegrationGroup& group,
                                                 bool includeRegistrationDependency) {
        if (lightsCalibrationDependencyRecomputes(group))
        {
            return QStringLiteral("Light calibration dependency will be recomputed");
        }
        if (includeRegistrationDependency && registrationDependencyRecomputes(group))
        {
            return QStringLiteral("Registration dependency will be recomputed");
        }
        return QString();
    };

    const auto integrationAction = [&](const WorkflowManifest::IntegrationGroup& group,
                                       bool includeRegistrationDependency,
                                       bool additionalDependencyRecomputes = false) {
        const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
        const bool registrationSelected = includeRegistrationDependency &&
                                          containsGroup(plan.registrationGroupsToProcess, group.registrationGroupKey);
        if (additionalDependencyRecomputes || integrationDependencyRecomputes(group, includeRegistrationDependency))
        {
            return PlanAction::Recompute;
        }
        return executionActionFor(plan, selected || registrationSelected, group.skipIfExists,
                                  outputPathFor(manifest, group.outputFilename), &snapshot);
    };

    const auto integrationReason = [&](const WorkflowManifest::IntegrationGroup& group, PlanAction action,
                                       bool includeRegistrationDependency, const QString& reuseReason) {
        QString reason = reasonMaps.integration.value(group.groupKey.toString());
        if (reason.trimmed().isEmpty())
        {
            reason = integrationDependencyReason(group, includeRegistrationDependency);
        }
        if (reason.trimmed().isEmpty() && includeRegistrationDependency &&
            containsGroup(plan.registrationGroupsToProcess, group.registrationGroupKey))
        {
            reason = QStringLiteral("Registration dependency will be recomputed");
        }
        return reasonForAction(plan, action, reason, reuseReason, previousManifest);
    };

    QHash<QString, AlignmentPlanStatus> alignment_status_cache;
    const auto alignmentStatusForGroup = [&](const WorkflowManifest::IntegrationGroup& group) {
        const QString cache_key = group.groupKey.toString();
        if (const auto cached = alignment_status_cache.constFind(cache_key); cached != alignment_status_cache.cend())
        {
            return cached.value();
        }
        AlignmentPlanStatus status = alignmentPlanStatusFor(manifest, plan, group, &snapshot);
        if (status.required && registrationDependencyRecomputes(group))
        {
            status.action = PlanAction::Recompute;
        }
        alignment_status_cache.insert(cache_key, status);
        return status;
    };

    if (stageEnabled(manifest, QStringLiteral("LightsAlignment")))
    {
        for (const auto& group : manifest.integrationGroups)
        {
            const AlignmentPlanStatus alignment_status = alignmentStatusForGroup(group);
            const QString groupDirName = ImageIntegration::sanitizeFilename(
                group.registrationGroupKey.toString().trimmed().isEmpty() ? group.groupKey.toString()
                                                                          : group.registrationGroupKey.toString());
            const QString outputs =
                QDir(manifest.outputDirectory).filePath(QStringLiteral("registered_lights/%1").arg(groupDirName));
            QString reason = !alignment_status.required
                                 ? QStringLiteral("Alignment outputs are not required for this group")
                                 : integrationReason(group, alignment_status.action, true,
                                                     QStringLiteral("Registered light outputs are current"));
            if (alignment_status.transformOnReadDecision.has_value())
            {
                const auto& routing = alignment_status.transformOnReadDecision.value();
                const QString policy = routing.usesTransformOnRead()
                                           ? QStringLiteral("Transform-on-read selected")
                                           : QStringLiteral("Materialized registered images selected");
                reason += QStringLiteral("; %1 (%2)").arg(policy, routing.reason);
                if (!routing.detail.isEmpty())
                {
                    reason += QStringLiteral(": %1").arg(routing.detail);
                }
            }
            auto decision = makeDecision(
                QStringLiteral("Lights Alignment"), group.groupKey.toCompactString(), alignment_status.action, reason,
                QStringLiteral("%1; Registration: %2")
                    .arg(frameCountText(static_cast<qsizetype>(group.lightFiles.size())),
                         group.registrationGroupKey.toCompactString()),
                estimateFrameCost(static_cast<qsizetype>(group.lightFiles.size()), 0.10), outputs);
            decision.transformOnReadDecision = alignment_status.transformOnReadDecision;
            decisions.push_back(std::move(decision));
        }
    }

    if (stageEnabled(manifest, QStringLiteral("LightsNormalization")))
    {
        for (const auto& group : manifest.integrationGroups)
        {
            const bool normalizationEnabled = group.stacking.normalizationMode != NormalizationMode::None ||
                                              !group.normalizationReferenceFrames.empty() ||
                                              group.normalizationCoefficientsArtifactPath.has_value();
            const bool handledDuringDrizzle = ImageIntegration::localNormalizationFittedByDrizzledStack(group);
            const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
            const AlignmentPlanStatus alignment_status = alignmentStatusForGroup(group);
            const bool alignmentRecomputes = alignment_status.action == PlanAction::Recompute;
            const bool dependencyRecomputes = alignmentRecomputes || integrationDependencyRecomputes(group, true);
            const QString dirtyReason = reasonMaps.integration.value(group.groupKey.toString());
            const bool canReuseExistingArtifact = normalizationEnabled && !handledDuringDrizzle && selected &&
                                                  !dependencyRecomputes &&
                                                  normalizationArtifactMetadataLooksReusable(manifest, group, snapshot);
            const PlanAction action =
                !normalizationEnabled
                    ? PlanAction::Skip
                    : (handledDuringDrizzle
                           ? PlanAction::Skip
                           : (dependencyRecomputes
                                  ? PlanAction::Recompute
                                  : (canReuseExistingArtifact ? PlanAction::Reuse : integrationAction(group, true))));
            const QString outputs = group.normalizationCoefficientsArtifactPath.value_or(
                QDir(manifest.outputDirectory)
                    .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization")));
            const QString reason =
                normalizationEnabled
                    ? (handledDuringDrizzle
                           ? QStringLiteral("The drizzled stack fits local normalization against the group's "
                                            "reference as it reads each light; the reference strategy, the "
                                            "evaluation, the PSF flux scale and the hot-pixel and noise filters do "
                                            "not apply")
                           : (canReuseExistingArtifact
                                  ? QStringLiteral("Existing normalization coefficients will be reused")
                                  : (alignmentRecomputes && dirtyReason.trimmed().isEmpty()
                                         ? QStringLiteral("Alignment outputs will be recomputed")
                                         : (dependencyRecomputes && dirtyReason.trimmed().isEmpty()
                                                ? integrationDependencyReason(group, true)
                                                : integrationReason(
                                                      group, action, true,
                                                      QStringLiteral("Normalization coefficients are current"))))))
                    : QStringLiteral("Normalization disabled for this group");
            decisions.push_back(
                makeDecision(QStringLiteral("Lights Normalization"), group.groupKey.toCompactString(), action, reason,
                             QStringLiteral("%1; References: %2")
                                 .arg(frameCountText(static_cast<qsizetype>(group.lightFiles.size())))
                                 .arg(group.normalizationReferenceFrames.size()),
                             estimateFrameCost(static_cast<qsizetype>(group.lightFiles.size()), 0.20), outputs));
            normalizationActionByIntegrationGroup.insert(group.groupKey.toString(), action);
        }
    }

    if (stageEnabled(manifest, QStringLiteral("LightsDrizzle")))
    {
        for (const auto& group : manifest.integrationGroups)
        {
            const bool drizzleEnabled = group.drizzle.has_value() && group.drizzle->enabled;
            const bool dependencyRecomputes = registrationDependencyRecomputes(group);
            const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
            const bool artifactsReusable = drizzleEnabled && selected && !dependencyRecomputes &&
                                           drizzleArtifactMetadataLooksReusable(manifest, group, snapshot);
            const PlanAction action =
                drizzleEnabled
                    ? (artifactsReusable ? PlanAction::Reuse : integrationAction(group, true, dependencyRecomputes))
                    : PlanAction::Skip;
            QStringList outputs;
            if (group.drizzlePixmapArtifactDir.has_value())
            {
                outputs << group.drizzlePixmapArtifactDir.value();
            }
            if (group.drizzleAccumulatorArtifact.has_value())
            {
                outputs << group.drizzleAccumulatorArtifact.value();
            }
            if (outputs.isEmpty())
            {
                outputs << QDir(manifest.outputDirectory)
                               .filePath(QStringLiteral(".afternight/image_integration/artifacts/drizzle"));
            }
            decisions.push_back(makeDecision(
                QStringLiteral("Lights Drizzle"), group.groupKey.toCompactString(), action,
                drizzleEnabled
                    ? (artifactsReusable
                           ? QStringLiteral("Persisted drizzle descriptors/maps match current transforms and settings")
                           : integrationReason(group, action, true, QStringLiteral("Drizzle artifacts are current")))
                    : QStringLiteral("Drizzle disabled for this group"),
                frameCountText(static_cast<qsizetype>(group.lightFiles.size())),
                estimateFrameCost(static_cast<qsizetype>(group.lightFiles.size()), 0.20),
                outputs.join(QStringLiteral("; "))));
            drizzleActionByIntegrationGroup.insert(group.groupKey.toString(), action);
        }
    }

    if (stageEnabled(manifest, QStringLiteral("LightsStacking")))
    {
        for (const auto& group : manifest.integrationGroups)
        {
            const AlignmentPlanStatus alignment_status = alignmentStatusForGroup(group);
            const bool alignmentRecomputes = alignment_status.action == PlanAction::Recompute;
            const bool normalizationRecomputes =
                normalizationActionByIntegrationGroup.value(group.groupKey.toString(), PlanAction::Skip) ==
                PlanAction::Recompute;
            const bool drizzleRecomputes = drizzleActionByIntegrationGroup.value(
                                               group.groupKey.toString(), PlanAction::Skip) == PlanAction::Recompute;
            const bool dependencyRecomputes = alignmentRecomputes || normalizationRecomputes || drizzleRecomputes;
            const bool selected = containsIntegrationGroup(plan.integrationGroupsToProcess, group.groupKey);
            const bool artifactReusable = selected && !dependencyRecomputes &&
                                          !integrationDependencyRecomputes(group, true) &&
                                          stackedOutputReusableForGroup(manifest, group, snapshot);
            const PlanAction action =
                artifactReusable ? PlanAction::Reuse : integrationAction(group, true, dependencyRecomputes);
            decisions.push_back(makeDecision(
                QStringLiteral("Lights Stacking"), group.groupKey.toCompactString(), action,
                artifactReusable ? QStringLiteral("Persisted stacked output matches current inputs and settings")
                                 : integrationReason(group, action, true, QStringLiteral("Stacked output is current")),
                frameCountText(static_cast<qsizetype>(group.lightFiles.size())),
                estimateFrameCost(static_cast<qsizetype>(group.lightFiles.size()), 0.30),
                outputPathFor(manifest, group.outputFilename)));
        }
    }

    return decisions;
}
