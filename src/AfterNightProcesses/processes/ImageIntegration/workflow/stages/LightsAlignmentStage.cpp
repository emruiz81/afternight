// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration workflow's alignment stage: each registered light read through the run's
/// registry, warped onto its group's plane with the source distortion its registration was solved over, and published
/// whole, stamped with the registration, input, canvas and sampling it satisfies.

#include "stdafx.hpp"
#include "LightsAlignmentStage.h"

#include "app/PlateSolveTypes.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/CometStackingProductResolver.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StageFramePipeline.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSqlQuery>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] const char* interpolationName(const an::KernelInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case an::KernelInterpolation::Nearest:
        return "Nearest";
    case an::KernelInterpolation::Linear:
        return "Bilinear";
    case an::KernelInterpolation::Cubic:
        return "Bicubic";
    case an::KernelInterpolation::Area:
        return "Area";
    case an::KernelInterpolation::Lanczos4:
        return "Lanczos4";
    case an::KernelInterpolation::LinearExact:
        return "LinearExact";
    case an::KernelInterpolation::NearestExact:
        return "NearestExact";
    }
    return "Unknown";
}

[[nodiscard]] bool cancelledStatus(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

[[nodiscard]] bool registeredOutputMatchesRevision(const QString& path, const qint64 expected_size,
                                                   const qint64 expected_modified_at_ms,
                                                   const QString& expected_identity)
{
    const auto revision = ArtifactTracker::captureFileRevision(path);
    return revision.has_value() && expected_size >= 0 && expected_modified_at_ms >= 0 && !expected_identity.isEmpty() &&
           revision->sizeBytes == expected_size && revision->modifiedAtMs == expected_modified_at_ms &&
           revision->identityToken == expected_identity;
}

bool hasIncrementalSelection(const WorkflowManifest& manifest)
{
    return manifest.executionMode == ExecutionMode::Incremental && !manifest.incrementalRegistrationGroups.empty();
}

bool isSelectedRegistrationGroup(const WorkflowManifest& manifest, const GroupKey& key)
{
    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        return true;
    }
    if (!hasIncrementalSelection(manifest))
    {
        return false;
    }
    return std::find(manifest.incrementalRegistrationGroups.begin(), manifest.incrementalRegistrationGroups.end(),
                     key) != manifest.incrementalRegistrationGroups.end();
}

bool isIncrementalIntegrationGroupSelected(const WorkflowManifest& manifest, const IntegrationGroupKey& key)
{
    if (manifest.executionMode != ExecutionMode::Incremental)
    {
        return false;
    }
    return std::find(manifest.incrementalIntegrationGroups.begin(), manifest.incrementalIntegrationGroups.end(), key) !=
           manifest.incrementalIntegrationGroups.end();
}

const WorkflowManifest::RegistrationGroup* findRegistrationGroup(const WorkflowManifest& manifest, const GroupKey& key)
{
    const auto it =
        std::find_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                     [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey == key; });
    return it == manifest.registrationGroups.end() ? nullptr : &*it;
}

/// The file a light is warped from, as it is now: the run's calibration and registration write it after the execution
/// plan is made.
QString alignmentInputPathFor(const WorkflowManifest& manifest, const WorkflowManifest::RegistrationGroup& reg_group,
                              const FrameId& frame)
{
    return registrationInputPathForFrame(manifest.outputDirectory, manifest.calibrateLightsInMemory, &reg_group, frame);
}

QString registeredPathForAlignmentInput(const QString& output_dir, const GroupKey& registration_group_key,
                                        const QString& input_path, const bool force_fits_output)
{
    const QFileInfo input_info(input_path);
    QString filename = input_info.completeBaseName() + QStringLiteral("_reg");
    const QString suffix = force_fits_output ? QStringLiteral("fits") : input_info.suffix();
    if (!suffix.trimmed().isEmpty())
    {
        filename += QLatin1Char('.') + suffix;
    }
    const QString group_dir_name = ImageIntegration::sanitizeFilename(registration_group_key.toString());
    return QDir(output_dir).filePath(QStringLiteral("registered_lights/%1/%2").arg(group_dir_name, filename));
}

bool sameCleanAbsolutePath(const QString& lhs, const QString& rhs)
{
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(lhs).absoluteFilePath())
               .compare(QDir::cleanPath(QFileInfo(rhs).absoluteFilePath()), kPathCase) == 0;
}

bool hasMissingRegisteredOutput(const WorkflowManifest& manifest,
                                const WorkflowManifest::IntegrationGroup& integration_group,
                                WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (integration_group.registrationGroupKey.toString().trimmed().isEmpty())
    {
        return false;
    }
    const auto* reg_group = findRegistrationGroup(manifest, integration_group.registrationGroupKey);
    if (reg_group == nullptr)
    {
        return false;
    }

    const bool force_fits_output = registrationGroupMaterializationUsesFits(manifest, *reg_group);
    std::unordered_map<QString, stage_utils::RegistrationOutputRecord> persisted_outputs;
    std::unordered_map<QString, an::RegistrationTransform> persisted_transforms;
    const QString metrics_path = QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    stage_utils::RegistrationMetricsReader metrics_reader(metrics_path, QStringLiteral("LightsAlignmentSkip"),
                                                          counters);
    bool metrics_loaded = false;
    if (metrics_reader.open())
    {
        const auto settings_version = metrics_reader.resolveSettingsVersionForGroup(
            integration_group.registrationGroupKey.toString(), reg_group->settings.version);
        QString transform_error;
        metrics_loaded = settings_version.has_value() &&
                         metrics_reader.loadOutputRecords(integration_group.registrationGroupKey.toString(),
                                                          *settings_version, persisted_outputs) &&
                         metrics_reader.loadTransforms(integration_group.registrationGroupKey.toString(),
                                                       *settings_version, persisted_transforms, transform_error) &&
                         stage_utils::rekeyRegistrationFrameLookupByIdentity(persisted_outputs) &&
                         stage_utils::rekeyRegistrationFrameLookupByIdentity(persisted_transforms);
    }

    for (const auto& frame : integration_group.lightFiles)
    {
        const QString input_path = alignmentInputPathFor(manifest, *reg_group, frame);
        if (input_path.trimmed().isEmpty())
        {
            continue;
        }
        const QString registered_path = registeredPathForAlignmentInput(
            manifest.outputDirectory, integration_group.registrationGroupKey, input_path, force_fits_output);
        if (!QFileInfo::exists(registered_path) || !metrics_loaded)
        {
            return true;
        }
        const auto frame_key = frame.identityKey();
        const auto persisted_it = persisted_outputs.find(frame_key);
        if (persisted_it == persisted_outputs.end() || !persisted_it->second.succeeded ||
            persisted_it->second.outputPath.trimmed().isEmpty() ||
            !sameCleanAbsolutePath(persisted_it->second.outputPath, registered_path) ||
            !registeredOutputMatchesRevision(persisted_it->second.outputPath, persisted_it->second.outputSizeBytes,
                                             persisted_it->second.outputModifiedAtMs,
                                             persisted_it->second.outputIdentityToken) ||
            persisted_transforms.find(frame_key) == persisted_transforms.end())
        {
            return true;
        }
    }
    return false;
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

bool shouldConsiderIntegrationGroupForAlignment(const WorkflowManifest& manifest,
                                                const WorkflowManifest::IntegrationGroup& group,
                                                const ExecutionPreparationSnapshot* snapshot,
                                                WorkflowExecutionCounterAccumulator* counters = nullptr)
{
    if (group.registrationGroupKey.toString().trimmed().isEmpty())
    {
        return false;
    }
    if (isDrizzleEnabled(group) && !cometNormalizationRequiresRegisteredOutputs(group))
    {
        return false;
    }
    if (ImageIntegration::planLightsTransformOnRead(manifest, group, snapshot).usesTransformOnRead())
    {
        // A candidate whose registrations still have to be confirmed before writing nothing is safe.
        return true;
    }
    if (manifest.executionMode != ExecutionMode::Incremental ||
        isSelectedRegistrationGroup(manifest, group.registrationGroupKey))
    {
        return true;
    }
    if (!isIncrementalIntegrationGroupSelected(manifest, group.groupKey))
    {
        return false;
    }
    if (cometAlignmentRequiresRegisteredOutputs(group))
    {
        return true;
    }
    return !ImageIntegration::registeredLightsCurrent(manifest, group, snapshot, counters);
}

/// Each frame's latest registration row: its transform, the distortion it was solved over and its aligned output.
struct FrameRecord
{
    QString transformJson;
    an::RegistrationTransform transform;
    stage_utils::SourceDistortionProvenance distortionProvenance;
    bool hasTransform = false;
    bool succeeded = false;
    QString outputPath;
    qint64 outputSizeBytes = -1;
    qint64 outputModifiedAtMs = -1;
    QString outputIdentityToken;
    qint64 updatedAt = 0;
};

[[nodiscard]] std::unordered_map<QString, FrameRecord> loadFrameRecords(core::SqliteConnection& connection,
                                                                        const QString& group_key, const int version)
{
    std::unordered_map<QString, FrameRecord> results;
    if (!connection.isOpen())
    {
        return results;
    }
    QSqlQuery query(connection.db());
    query.prepare(QStringLiteral("SELECT frame_id, transform_json, output_path, output_size_bytes, "
                                 "output_modified_at_ms, output_identity_token, updated_at, succeeded FROM "
                                 "registration_metrics WHERE group_key = :key AND settings_version = :ver"));
    query.bindValue(QStringLiteral(":key"), group_key);
    query.bindValue(QStringLiteral(":ver"), version);
    if (!connection.execute(query))
    {
        return results;
    }
    struct Selected
    {
        QString storedFrameId;
        FrameRecord record;
    };
    std::unordered_map<QString, Selected> selected_by_identity;
    while (query.next())
    {
        const QString stored_frame_id = query.value(0).toString();
        const auto frame_id = stage_utils::persistedRegistrationFrameId(stored_frame_id);
        if (!frame_id.has_value())
        {
            continue;
        }
        FrameRecord record;
        record.transformJson = query.value(1).toString();
        record.outputPath = query.value(2).toString();
        record.outputSizeBytes = query.value(3).toLongLong();
        record.outputModifiedAtMs = query.value(4).toLongLong();
        record.outputIdentityToken = query.value(5).toString();
        record.updatedAt = query.value(6).toLongLong();
        record.succeeded = query.value(7).toInt() != 0;
        if (record.succeeded && !record.transformJson.trimmed().isEmpty())
        {
            QString parse_error;
            record.hasTransform = stage_utils::parseRegistrationTransformJson(
                record.transformJson, record.transform, &record.distortionProvenance, parse_error);
        }
        const auto selected = selected_by_identity.find(frame_id->identityKey());
        if (selected == selected_by_identity.end() ||
            stage_utils::preferPersistedRegistrationRow(
                record.updatedAt, stored_frame_id, selected->second.record.updatedAt, selected->second.storedFrameId))
        {
            selected_by_identity.insert_or_assign(frame_id->identityKey(),
                                                  Selected{stored_frame_id, std::move(record)});
        }
    }
    for (auto& [identity, selected] : selected_by_identity)
    {
        results.emplace(identity, std::move(selected.record));
    }
    return results;
}

/// The distortion a record's provenance resolves to for a frame whose header states `header`.
[[nodiscard]] std::optional<an::SipDistortion>
resolvedDistortion(const stage_utils::SourceDistortionProvenance& provenance,
                   const std::optional<an::SipDistortion>& header)
{
    switch (provenance.policy)
    {
    case stage_utils::SourceDistortionPolicy::ExplicitModel:
        return provenance.model;
    case stage_utils::SourceDistortionPolicy::InheritMetadata:
        return header;
    case stage_utils::SourceDistortionPolicy::ExplicitNone:
        break;
    }
    return std::nullopt;
}

/// The identity a registered light satisfies: its family, the registration it was warped by, the file it was read from,
/// the plane, the sampling and the solution the plane carries.
[[nodiscard]] QString registeredLightArtifactHash(const QString& transform_json, const QString& input_path,
                                                  const an::RegistrationCanvas& canvas,
                                                  const an::KernelInterpolation interpolation,
                                                  const QString& plane_solution_fingerprint, const QString& role)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schemaVersion"), 1);
    ImageIntegration::schema::insertArtifactFamilyIdentity(obj,
                                                           ImageIntegration::schema::ArtifactFamily::RegisteredLight);
    obj.insert(QStringLiteral("role"), role);
    obj.insert(QStringLiteral("transform"), transform_json);
    QJsonObject input;
    const QString clean_path = QDir::cleanPath(input_path);
    input.insert(QStringLiteral("path"), clean_path);
    const QFileInfo info(clean_path);
    if (info.exists())
    {
        input.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
        input.insert(QStringLiteral("mtime"), info.lastModified().toMSecsSinceEpoch());
    }
    obj.insert(QStringLiteral("input"), input);
    obj.insert(QStringLiteral("canvasOriginX"), canvas.origin.x);
    obj.insert(QStringLiteral("canvasOriginY"), canvas.origin.y);
    obj.insert(QStringLiteral("canvasWidth"), static_cast<qint64>(canvas.width));
    obj.insert(QStringLiteral("canvasHeight"), static_cast<qint64>(canvas.height));
    obj.insert(QStringLiteral("interpolation"), QString::fromLatin1(interpolationName(interpolation)));
    obj.insert(QStringLiteral("planeSolution"), plane_solution_fingerprint);
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(obj).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}

/// A fingerprint of a solution's cards, empty for none.
[[nodiscard]] QString solutionFingerprint(const std::optional<an::WcsSolution>& solution)
{
    if (!solution.has_value())
    {
        return {};
    }
    const an::Result<std::vector<an::CodecCard>> cards = an::buildWcsCards(*solution);
    if (!cards.hasValue())
    {
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (const an::CodecCard& card : *cards)
    {
        digest.addData(QByteArray::fromStdString(card.keyword()));
        digest.addData(QByteArrayLiteral("="));
        digest.addData(QByteArray::fromStdString(card.value()));
        digest.addData(QByteArrayLiteral("\n"));
    }
    return QString::fromLatin1(digest.result().toHex());
}

/// One frame to warp: its frame, the file it is read from, its transform and distortion, and where it is written.
struct AlignmentWork
{
    FrameId frame;
    QString inputPath;
    QString transformJson;
    an::RegistrationTransform transform;
    std::optional<an::SipDistortion> distortion;
    QString outputPath;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

/// What warping a set of frames produced.
struct AlignmentBatch
{
    int succeeded = 0;
    int sipCorrected = 0;
    std::vector<std::pair<FrameId, FileRevisionStamp>> outputs;
    QStringList failures;
    bool cancelled = false;
};

/// Warps each frame onto the plane in its own admitted operation, calibrating a light read as it is when the run
/// calibrates in memory, and publishes it stamped with what it satisfies.
///
/// When the run is wide enough, the next frame is read and calibrated, and the last one written, while this one is
/// warped, each in an operation of its own at its share of the run's width (`stage_utils::pipelineWidths`). What each
/// frame produced is recorded under its own position, so the batch lists the outputs and failures in the frames' order
/// whichever writer finished first.
[[nodiscard]] AlignmentBatch
alignFrames(const ImageIntegration::WorkflowStageServices& services, const WorkflowManifest& manifest,
            core::CalibrationMasterSet& masters, const ExecutionPreparationSnapshot* snapshot,
            const std::vector<AlignmentWork>& work, const an::RegistrationCanvas& canvas,
            const an::KernelInterpolation interpolation, const std::optional<an::WcsSolution>& plane_solution,
            const an::ui::processes::RegistrationDemosaic& demosaic, const QString& role, CancellationToken& token,
            const std::function<void(int, const QString&)>& report)
{
    AlignmentBatch batch;
    const an::CodecRegistry* registry = services.codecRegistry().get();
    const QString plane_fingerprint = solutionFingerprint(plane_solution);

    // A light read and calibrated as the run calibrates it, through the master set; one thread at a time reads.
    const auto read_frame = [&](const an::consumer::OperationScope& scope,
                                const AlignmentWork& item) -> an::Result<an::Image<float>> {
        const WorkflowManifest::LightsCalibrationParams* params =
            snapshot != nullptr ? snapshot->calibrationAssignment(item.frame) : nullptr;
        if (params == nullptr)
        {
            for (const auto& candidate : manifest.lightsCalibration)
            {
                if (candidate.lightPath == item.frame)
                {
                    params = &candidate;
                    break;
                }
            }
        }
        std::shared_ptr<const core::LightCalibration> prepared_calibration;
        if (manifest.calibrateLightsInMemory && params != nullptr &&
            sameCleanAbsolutePath(item.inputPath, item.frame.canonicalPath()))
        {
            an::Result<std::shared_ptr<const core::LightCalibration>> prepared = masters.prepare(scope, *params);
            if (!prepared.hasValue())
            {
                return prepared.status();
            }
            prepared_calibration = std::move(*prepared);
        }
        return stage_utils::readRegistrationImage(scope, registry, item.inputPath, prepared_calibration.get(),
                                                  demosaic);
    };
    const auto warp_frame = [&](const an::consumer::OperationScope& scope, const AlignmentWork& item,
                                const an::Image<float>& image) {
        return stage_utils::warpRegistrationFrame(scope, image, item.transform, item.distortion, canvas, interpolation,
                                                  plane_solution);
    };
    const auto write_frame = [&](const an::consumer::OperationScope& scope, const AlignmentWork& item,
                                 const an::Image<float>& warped) -> an::Status {
        const QString artifact_hash = registeredLightArtifactHash(item.transformJson, item.inputPath, canvas,
                                                                  interpolation, plane_fingerprint, role);
        an::Result<an::ArtifactIdentity> identity =
            ImageIntegration::workflowArtifactIdentity(artifact_hash, manifest.executionId);
        if (!identity.hasValue())
        {
            return identity.status();
        }
        return ImageIntegration::publishWorkflowImage(scope, warped, item.outputPath, *identity);
    };

    // What became of each frame, at its position: whether it was reached, its outcome and the published revision.
    struct FrameOutcome
    {
        bool reached = false;
        an::Status status;
        std::optional<FileRevisionStamp> revision;
    };
    std::vector<FrameOutcome> outcomes(work.size());
    const auto finish = [&](const std::size_t index, an::Status status) {
        FrameOutcome& outcome = outcomes[index];
        outcome.reached = true;
        if (status.ok())
        {
            outcome.revision = ArtifactTracker::captureFileRevision(work[index].outputPath);
        }
        outcome.status = std::move(status);
    };

    const stage_utils::PipelineWidths widths = stage_utils::pipelineWidths(services.runWidth(), true);
    if (!widths.overlapped)
    {
        int done = 0;
        for (std::size_t index = 0u; index < work.size(); ++index)
        {
            if (token.isCancellationRequested())
            {
                batch.cancelled = true;
                break;
            }
            const AlignmentWork& item = work[index];
            an::Status outcome = [&]() -> an::Status {
                an::Result<an::consumer::OperationScope> scope = services.admitStageOperation();
                if (!scope.hasValue())
                {
                    return scope.status();
                }
                an::Result<an::Image<float>> image = read_frame(*scope, item);
                if (!image.hasValue())
                {
                    return image.status();
                }
                an::Result<an::Image<float>> warped = warp_frame(*scope, item, *image);
                if (!warped.hasValue())
                {
                    return warped.status();
                }
                return write_frame(*scope, item, *warped);
            }();
            const bool stopped = cancelledStatus(outcome);
            finish(index, std::move(outcome));
            if (stopped)
            {
                batch.cancelled = true;
                break;
            }
            ++done;
            if (report)
            {
                report(done, item.frame.toString());
            }
        }
    }
    else
    {
        // A frame read ahead: the light as calibrated, or why it could not be.
        struct ReadFrame
        {
            an::Status status;
            std::optional<an::Image<float>> image;
        };
        // A frame warped and waiting to be written.
        struct WarpedFrame
        {
            std::size_t index = 0u;
            std::optional<an::Image<float>> image;
        };
        // Declared before the reader, so that a stop leaves the reader first and the writes already handed over last.
        stage_utils::WriteBehind<WarpedFrame> writer(widths.writers, widths.writers + 1u, [&](WarpedFrame&& frame) {
            an::Status written = [&]() -> an::Status {
                an::Result<an::consumer::OperationScope> scope = services.admitStageOperationAtWidth(1u);
                if (!scope.hasValue())
                {
                    return scope.status();
                }
                return write_frame(*scope, work[frame.index], *frame.image);
            }();
            frame.image.reset();
            finish(frame.index, std::move(written));
        });
        stage_utils::ReadAhead<ReadFrame> reader(work.size(), 1u, [&](const std::size_t index, std::stop_token) {
            ReadFrame read;
            an::Result<an::consumer::OperationScope> scope = services.admitStageOperationAtWidth(widths.reader);
            if (!scope.hasValue())
            {
                read.status = scope.status();
                return read;
            }
            an::Result<an::Image<float>> image = read_frame(*scope, work[index]);
            if (image.hasValue())
            {
                read.image.emplace(std::move(*image));
            }
            else
            {
                read.status = image.status();
            }
            return read;
        });
        int done = 0;
        for (std::size_t index = 0u; index < work.size(); ++index)
        {
            if (token.isCancellationRequested())
            {
                batch.cancelled = true;
                break;
            }
            ReadFrame read = reader.take();
            if (read.status.failed())
            {
                const bool stopped = cancelledStatus(read.status);
                finish(index, std::move(read.status));
                if (stopped)
                {
                    batch.cancelled = true;
                    break;
                }
            }
            else
            {
                an::Result<an::Image<float>> warped = [&]() -> an::Result<an::Image<float>> {
                    an::Result<an::consumer::OperationScope> scope =
                        services.admitStageOperationAtWidth(widths.compute);
                    if (!scope.hasValue())
                    {
                        return scope.status();
                    }
                    return warp_frame(*scope, work[index], *read.image);
                }();
                read.image.reset();
                if (!warped.hasValue())
                {
                    const bool stopped = cancelledStatus(warped.status());
                    finish(index, warped.status());
                    if (stopped)
                    {
                        batch.cancelled = true;
                        break;
                    }
                }
                else
                {
                    writer.submit(WarpedFrame{.index = index, .image = std::move(*warped)});
                }
            }
            ++done;
            if (report)
            {
                report(done, work[index].frame.toString());
            }
        }
        writer.drain();
    }

    for (std::size_t index = 0u; index < work.size(); ++index)
    {
        const FrameOutcome& outcome = outcomes[index];
        const AlignmentWork& item = work[index];
        if (!outcome.reached)
        {
            continue;
        }
        if (outcome.status.failed())
        {
            if (cancelledStatus(outcome.status))
            {
                batch.cancelled = true;
                continue;
            }
            batch.failures << QStringLiteral("%1: %2").arg(QFileInfo(item.inputPath).fileName(),
                                                           ImageIntegration::engineFailureText(outcome.status));
        }
        else if (outcome.revision.has_value())
        {
            ++batch.succeeded;
            batch.sipCorrected += item.distortion.has_value() ? 1 : 0;
            batch.outputs.emplace_back(item.frame, *outcome.revision);
        }
        else
        {
            batch.failures << QStringLiteral("%1: the published output could not be found").arg(item.outputPath);
        }
    }
    return batch;
}

StageResult materializeCometAlignedOutputs(
    const ImageIntegration::WorkflowStageServices& services, const WorkflowManifest& manifest,
    const WorkflowManifest::RegistrationGroup& reg_group, const WorkflowManifest::IntegrationGroup& integration_group,
    const QString& metrics_path, const QDir& registered_root, core::CalibrationMasterSet& masters,
    const ExecutionPreparationSnapshot* snapshot, CancellationToken& token, ProgressReporter& reporter,
    const int group_index, const int total_groups, WorkflowExecutionCounterAccumulator* counters)
{
    const QString reg_key = reg_group.groupKey.toString();
    an::ui::RegistrationMetricsStateLoader loader(metrics_path, QStringLiteral("LightsAlignmentCometLoader"), counters);
    QString error;
    if (!loader.open(&error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics database"),
                                                     metrics_path, error));
    }
    const auto group_state = loader.loadGroupState(reg_key, reg_group.settings.version, &error);
    if (!group_state.has_value())
    {
        return StageResult::failure(IntegrationError::registrationFailed(
            error.trimmed().isEmpty()
                ? QStringLiteral("Comet alignment materialization requires registration group state for '%1'.")
                      .arg(reg_key)
                : error));
    }

    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = metrics_path;
    request.groupKey = group_state->groupKey;
    request.registrationHash = group_state->registrationHash;
    request.lightFilesHash = group_state->lightFilesHash;
    request.calibrationHash = group_state->calibrationHash;
    request.resolvedReferenceFrameId = group_state->referenceFrameId;
    request.stackingMode = QStringLiteral("Comet");
    request.role = QStringLiteral("comet_aligned");
    request.drizzle = true;
    request.frameIds = integration_group.lightFiles;
    request.counters = counters;
    const an::ui::CometStackingProductResolveResult products = an::ui::resolveCometStackingProducts(request);
    if (!products.available || products.kind != an::ui::CometStackingProductKind::TransformRoles ||
        !products.outputPlane.has_value() || !products.readiness.has_value())
    {
        const QString detail = products.errorMessage.trimmed().isEmpty()
                                   ? QStringLiteral("active comet transform products are unavailable")
                                   : products.errorMessage;
        return StageResult::failure(IntegrationError::registrationFailed(
            QStringLiteral("Comet alignment materialization requires active comet_aligned transforms: %1")
                .arg(detail)));
    }
    const an::ui::CometOutputPlaneRecord& plane = products.outputPlane.value();
    if (plane.canvasWidth <= 0 || plane.canvasHeight <= 0)
    {
        return StageResult::failure(
            IntegrationError::registrationFailed(QStringLiteral("Comet output plane '%1' has invalid dimensions %2x%3.")
                                                     .arg(plane.cometOutputPlaneId)
                                                     .arg(plane.canvasWidth)
                                                     .arg(plane.canvasHeight)));
    }
    const an::RegistrationCanvas canvas{.origin =
                                            an::PixelCoordinate{.x = plane.canvasOffsetX, .y = plane.canvasOffsetY},
                                        .width = static_cast<std::uint32_t>(plane.canvasWidth),
                                        .height = static_cast<std::uint32_t>(plane.canvasHeight)};

    std::unordered_map<QString, FrameId> frames_by_id;
    for (const FrameId& frame : integration_group.lightFiles)
    {
        frames_by_id.emplace(frame.canonicalPath(), frame);
    }
    const QString run_dir_path = registered_root.filePath(
        QStringLiteral("%1/comet_aligned/%2")
            .arg(ImageIntegration::sanitizeFilename(reg_key), ImageIntegration::sanitizeFilename(products.cometRunId)));
    if (!QDir().mkpath(run_dir_path))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(run_dir_path));
    }

    std::vector<AlignmentWork> work;
    std::unordered_map<QString, an::ui::CometTransformRoleRecord> transforms_by_frame;
    std::vector<QString> input_paths;
    for (const an::ui::CometTransformRoleRecord& record : products.transforms)
    {
        const auto frame_it = frames_by_id.find(record.frameId.canonicalPath());
        if (frame_it == frames_by_id.end())
        {
            return StageResult::failure(IntegrationError::registrationFailed(
                QStringLiteral("Comet alignment transform references unexpected frame '%1'.")
                    .arg(record.frameId.canonicalPath())));
        }
        const QString input_path = alignmentInputPathFor(manifest, reg_group, frame_it->second);
        if (input_path.trimmed().isEmpty() || !QFileInfo::exists(input_path))
        {
            return StageResult::failure(IntegrationError::fileNotFound(input_path));
        }
        AlignmentWork item;
        item.frame = frame_it->second;
        item.inputPath = input_path;
        item.transformJson = record.transformJson;
        stage_utils::SourceDistortionProvenance provenance;
        QString parse_error;
        if (!stage_utils::parseRegistrationTransformJson(record.transformJson, item.transform, &provenance,
                                                         parse_error))
        {
            return StageResult::failure(IntegrationError::registrationFailed(
                QStringLiteral("Failed to parse comet_aligned transform for '%1': %2")
                    .arg(record.frameId.canonicalPath(), parse_error)));
        }
        if (!record.sourceDistortionJson.trimmed().isEmpty())
        {
            const QJsonDocument document = QJsonDocument::fromJson(record.sourceDistortionJson.toUtf8());
            an::SipDistortion distortion;
            if (!document.isObject() || !stage_utils::sipDistortionFromJson(document.object(), distortion))
            {
                return StageResult::failure(IntegrationError::registrationFailed(
                    QStringLiteral("Failed to parse comet_aligned source distortion for '%1'.")
                        .arg(record.frameId.canonicalPath())));
            }
            item.distortion = distortion;
        }
        else if (provenance.policy == stage_utils::SourceDistortionPolicy::ExplicitModel)
        {
            item.distortion = provenance.model;
        }
        input_paths.push_back(input_path);
        transforms_by_frame.emplace(frame_it->second.canonicalPath(), record);
        work.push_back(std::move(item));
    }
    if (work.empty())
    {
        return StageResult::failure(IntegrationError::registrationFailed(
            QStringLiteral("No comet_aligned frames were selected for alignment.")));
    }
    const bool force_fits = std::any_of(input_paths.begin(), input_paths.end(), [](const QString& path) {
        return !ImageIntegration::isFitsExtension(QFileInfo(path).suffix());
    });
    for (AlignmentWork& item : work)
    {
        const QFileInfo input_info(item.inputPath);
        item.outputPath = QDir(run_dir_path)
                              .filePath(input_info.completeBaseName() + QStringLiteral("_comet_reg.") +
                                        (force_fits ? QStringLiteral("fits") : input_info.suffix()));
    }

    const an::ui::processes::RegistrationDemosaic demosaic =
        an::ui::processes::registrationDemosaicOf(reg_group.settings);
    const an::Result<an::KernelInterpolation> interpolation =
        an::ui::processes::alignmentInterpolationOf(reg_group.settings.interpolation);
    const an::KernelInterpolation sampling =
        interpolation.hasValue() ? *interpolation : an::KernelInterpolation::Lanczos4;
    const int total = static_cast<int>(work.size());
    // A comet-aligned light carries no world coordinates: its plane follows the comet, not the sky.
    const AlignmentBatch batch =
        alignFrames(services, manifest, masters, snapshot, work, canvas, sampling, std::nullopt, demosaic,
                    QStringLiteral("comet_aligned"), token, [&](const int done, const QString& file) {
                        stage_utils::reportStageProgress(
                            reporter, WorkflowStage::LightsAlignment, group_index, total_groups,
                            static_cast<float>(done) / static_cast<float>((std::max)(1, total)),
                            QStringLiteral("Materializing comet-aligned image %1 of %2").arg(done).arg(total), file);
                    });
    if (batch.cancelled || token.isCancellationRequested())
    {
        return StageResult::cancelled();
    }

    an::ui::RegistrationMetricsWriter writer(metrics_path, QStringLiteral("LightsAlignmentCometOutputs"), counters);
    if (!writer.initialize(&error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics database"),
                                                     metrics_path, error));
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    std::unordered_map<QString, FileRevisionStamp> outputs_by_frame;
    for (const auto& [frame, revision] : batch.outputs)
    {
        outputs_by_frame.emplace(frame.canonicalPath(), revision);
    }
    int failed_outputs = 0;
    QStringList failure_messages = batch.failures;
    for (const AlignmentWork& item : work)
    {
        const an::ui::CometTransformRoleRecord& transform = transforms_by_frame.at(item.frame.canonicalPath());
        an::ui::CometRegisteredOutputRecord output;
        output.cometRunId = products.cometRunId;
        output.frameId = item.frame;
        output.role = QStringLiteral("comet_aligned");
        output.cometOutputPlaneId = plane.cometOutputPlaneId;
        output.updatedAt = now;
        output.artifactHash = registeredLightArtifactHash(transform.transformJson, item.inputPath, canvas, sampling,
                                                          plane.sourceFingerprint, QStringLiteral("comet_aligned"));
        if (const auto produced = outputs_by_frame.find(item.frame.canonicalPath()); produced != outputs_by_frame.end())
        {
            output.succeeded = true;
            output.outputPath = produced->second.path;
            output.outputSizeBytes = produced->second.sizeBytes;
            output.outputModifiedAtMs = produced->second.modifiedAtMs;
            output.outputIdentityToken = produced->second.identityToken;
        }
        else
        {
            output.succeeded = false;
            output.outputPath = item.outputPath;
            output.failureReason = QStringLiteral("The comet-aligned light was not written");
            ++failed_outputs;
        }
        if (!writer.upsertCometRegisteredOutput(output, &error))
        {
            return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                         QStringLiteral("Failed to write comet registered output"),
                                                         metrics_path, error));
        }
    }

    an::ui::CometRoleReadinessRecord readiness = products.readiness.value();
    readiness.requiresMaterializedOutputs = true;
    readiness.materializedOutputCount = batch.succeeded;
    readiness.failedOutputCount = failed_outputs;
    readiness.missingOutputCount = (std::max)(0, readiness.selectedFrameCount - batch.succeeded - failed_outputs);
    // The readiness table accepts Failed and OutputMissing for a role whose outputs are not all written.
    readiness.readinessStatus = failed_outputs > 0                 ? QStringLiteral("Failed")
                                : readiness.missingOutputCount > 0 ? QStringLiteral("OutputMissing")
                                                                   : QStringLiteral("Ready");
    readiness.failureReason = failure_messages.join(QStringLiteral("; "));
    readiness.updatedAt = now;
    if (!writer.upsertCometRoleReadiness(readiness, &error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to update comet role readiness"),
                                                     metrics_path, error));
    }
    if (failed_outputs > 0 || readiness.missingOutputCount > 0)
    {
        return StageResult::failure(IntegrationError::registrationFailed(
            readiness.failureReason.trimmed().isEmpty()
                ? QStringLiteral("Comet alignment materialization failed for one or more frames.")
                : readiness.failureReason));
    }
    StageResult result = StageResult::success();
    AlignmentGroupResult group_result;
    group_result.groupKey = reg_key;
    group_result.totalAligned = batch.succeeded;
    group_result.sipCorrectedCount = batch.sipCorrected;
    result.alignmentGroupResults.push_back(std::move(group_result));
    return result;
}

} // namespace

bool ImageIntegration::registeredLightsCurrent(const WorkflowManifest& manifest,
                                               const WorkflowManifest::IntegrationGroup& group,
                                               const ExecutionPreparationSnapshot* snapshot,
                                               WorkflowExecutionCounterAccumulator* counters)
{
    if (group.registrationGroupKey.toString().trimmed().isEmpty())
    {
        return true;
    }
    const auto* registration_group = findRegistrationGroup(manifest, group.registrationGroupKey);
    if (registration_group == nullptr)
    {
        return false;
    }
    if (snapshot == nullptr)
    {
        return !hasMissingRegisteredOutput(manifest, group, counters);
    }

    const bool force_fits_output = registrationGroupMaterializationUsesFits(manifest, *registration_group, snapshot);
    const auto settings_version = snapshot->resolveRegistrationSettingsVersion(group.registrationGroupKey.toString(),
                                                                               registration_group->settings.version);
    const auto* metrics = settings_version.has_value()
                              ? snapshot->registrationMetrics(group.registrationGroupKey.toString(), *settings_version)
                              : nullptr;
    if (metrics == nullptr)
    {
        return false;
    }
    for (const FrameId& frame : group.lightFiles)
    {
        const auto metric = metrics->constFind(frame);
        if (metric == metrics->cend() || !metric->succeeded || !metric->transform.has_value() ||
            metric->outputPath.trimmed().isEmpty())
        {
            return false;
        }
        const QString expected_path = registeredPathForAlignmentInput(
            manifest.outputDirectory, group.registrationGroupKey,
            registrationInputPathForFrame(manifest.outputDirectory, manifest.calibrateLightsInMemory,
                                          registration_group, frame, snapshot),
            force_fits_output);
        const auto* output_stamp = snapshot->fileStamp(metric->outputPath);
        if (output_stamp == nullptr || !output_stamp->isFile ||
            !sameCleanAbsolutePath(metric->outputPath, expected_path) || metric->outputSizeBytes < 0 ||
            metric->outputModifiedAtMs < 0 || metric->outputIdentityToken.isEmpty() ||
            output_stamp->sizeBytes != metric->outputSizeBytes ||
            output_stamp->modifiedAtMs != metric->outputModifiedAtMs ||
            output_stamp->identityToken != metric->outputIdentityToken)
        {
            return false;
        }
    }
    return true;
}

QString LightsAlignmentStage::name() const
{
    return QStringLiteral("LightsAlignment");
}

WorkflowStage LightsAlignmentStage::stageId() const
{
    return WorkflowStage::LightsAlignment;
}

bool LightsAlignmentStage::shouldSkip(const WorkflowManifest& manifest) const
{
    if (manifest.integrationGroups.empty())
    {
        return true;
    }
    return std::none_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                        [&](const WorkflowManifest::IntegrationGroup& group) {
                            return shouldConsiderIntegrationGroupForAlignment(
                                manifest, group, executionPreparationSnapshot(), executionCounterAccumulator());
                        });
}

QString LightsAlignmentStage::skipReason(const WorkflowManifest& manifest) const
{
    if (manifest.integrationGroups.empty())
    {
        return QStringLiteral("No integration groups.");
    }
    if (manifest.executionMode == ExecutionMode::Incremental && manifest.incrementalRegistrationGroups.empty())
    {
        return QStringLiteral("No dirty registration groups in incremental plan.");
    }
    return QStringLiteral("No selected integration groups require alignment outputs.");
}

StageResult LightsAlignmentStage::execute(const WorkflowManifest& manifest, CancellationToken& token,
                                          ProgressReporter& reporter)
{
    const ExecutionPreparationSnapshot* snapshot = executionPreparationSnapshot();
    // Alignment runs per registration group: integration groups sharing one share its plane, so every registered
    // light of the group has one size.
    std::set<QString> registration_keys_to_process;
    for (const auto& group : manifest.integrationGroups)
    {
        if (!shouldConsiderIntegrationGroupForAlignment(manifest, group, snapshot, executionCounterAccumulator()))
        {
            continue;
        }
        const QString reg_key = group.registrationGroupKey.toString().trimmed();
        if (!reg_key.isEmpty())
        {
            registration_keys_to_process.insert(reg_key);
        }
    }
    std::vector<const WorkflowManifest::RegistrationGroup*> groups_to_process;
    for (const auto& group : manifest.registrationGroups)
    {
        if (registration_keys_to_process.contains(group.groupKey.toString()))
        {
            groups_to_process.push_back(&group);
        }
    }
    if (groups_to_process.empty())
    {
        return StageResult::skipped();
    }
    const ImageIntegration::WorkflowStageServices* services = stageServices();
    if (services == nullptr)
    {
        return StageResult::failure(IntegrationError::internalError(
            QStringLiteral("The lights alignment stage runs only inside a workflow run, which lends it the session")));
    }

    const int total_groups = static_cast<int>(groups_to_process.size());
    reporter.logger().stageStarted(name(), total_groups);
    stage_utils::reportStageProgress(reporter, stageId(), 1, (std::max)(1, total_groups), 0.0f,
                                     QStringLiteral("Preparing alignment"), QString());
    QElapsedTimer stage_timer;
    stage_timer.start();

    const QDir output_root(manifest.outputDirectory);
    const QString metrics_path = output_root.filePath(QStringLiteral("registration_metrics.db"));
    core::SqliteConnection records_connection(metrics_path, QStringLiteral("LightsAlignmentStage"),
                                              executionCounterAccumulator());
    if (!records_connection.open(nullptr))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics database"),
                                                     metrics_path));
    }
    an::ui::RegistrationMetricsWriter metrics_writer(metrics_path, QStringLiteral("LightsAlignmentOutputPaths"),
                                                     executionCounterAccumulator());
    QString metrics_writer_error;
    if (!metrics_writer.openExisting(&metrics_writer_error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics writer"),
                                                     metrics_path, metrics_writer_error));
    }
    stage_utils::RegistrationMetricsReader metrics_reader(metrics_path, QStringLiteral("LightsAlignmentWcs"),
                                                          executionCounterAccumulator());
    QString metrics_reader_error;
    if (!metrics_reader.open(&metrics_reader_error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics reader"),
                                                     metrics_path, metrics_reader_error));
    }
    an::ui::RegistrationMetricsStateLoader state_loader(metrics_path, QStringLiteral("LightsAlignmentState"),
                                                        executionCounterAccumulator());
    if (!state_loader.open(&metrics_reader_error))
    {
        return StageResult::failure(IntegrationError(IntegrationErrorCode::FileNotWritable,
                                                     QStringLiteral("Failed to open registration metrics reader"),
                                                     metrics_path, metrics_reader_error));
    }
    const QString registered_root_path = output_root.filePath(QStringLiteral("registered_lights"));
    const QDir registered_root(registered_root_path);
    if (!registered_root.exists() && !QDir().mkpath(registered_root_path))
    {
        return StageResult::failure(IntegrationError::directoryNotWritable(registered_root_path));
    }
    core::CalibrationMasterSet masters(services->codecRegistry());

    int group_index = 0;
    IntegrationErrorReport error_report;
    std::vector<AlignmentGroupResult> all_alignment_results;
    int failed_groups = 0;
    int successful_groups = 0;

    for (const auto* reg_group : groups_to_process)
    {
        ++group_index;
        if (token.isCancellationRequested())
        {
            return StageResult::cancelled();
        }
        const QString group_label = reg_group->groupKey.toCompactString();
        reporter.logger().groupStarted(group_index, total_groups, group_label);
        QElapsedTimer group_timer;
        group_timer.start();

        const auto process_group = [&]() -> StageResult {
            const QString reg_key = reg_group->groupKey.toString();
            std::vector<AlignmentGroupResult> comet_alignment_results;
            for (const auto& integration_group : manifest.integrationGroups)
            {
                if (integration_group.registrationGroupKey.toString() != reg_key ||
                    !shouldConsiderIntegrationGroupForAlignment(manifest, integration_group, snapshot,
                                                                executionCounterAccumulator()) ||
                    !cometAlignmentRequiresRegisteredOutputs(integration_group))
                {
                    continue;
                }
                StageResult comet_result = materializeCometAlignedOutputs(
                    *services, manifest, *reg_group, integration_group, metrics_path, registered_root, masters,
                    snapshot, token, reporter, group_index, total_groups, executionCounterAccumulator());
                if (!comet_result.isSuccess())
                {
                    return comet_result;
                }
                for (auto& alignment_result : comet_result.alignmentGroupResults)
                {
                    comet_alignment_results.push_back(std::move(alignment_result));
                }
            }
            const auto success_with_comet = [&]() {
                StageResult result = StageResult::success();
                result.alignmentGroupResults = comet_alignment_results;
                return result;
            };

            const bool ordinary_alignment_required =
                std::any_of(manifest.integrationGroups.begin(), manifest.integrationGroups.end(),
                            [&](const WorkflowManifest::IntegrationGroup& group) {
                                return group.registrationGroupKey.toString() == reg_key &&
                                       shouldConsiderIntegrationGroupForAlignment(manifest, group, snapshot,
                                                                                  executionCounterAccumulator()) &&
                                       !ImageIntegration::usesCometOnlyStackingMode(group.stacking.cometStackingMode);
                            });
            if (!ordinary_alignment_required)
            {
                return success_with_comet();
            }

            const int preferred_version = reg_group->settings.version;
            const auto resolved_version = metrics_reader.resolveSettingsVersionForGroup(reg_key, preferred_version);
            if (!resolved_version.has_value())
            {
                reporter.logger().warning(
                    QStringLiteral("No registration metrics found for group '%1' (requested settings v%2). Skipping.")
                        .arg(group_label)
                        .arg(preferred_version));
                return success_with_comet();
            }
            const int settings_version = *resolved_version;
            if (settings_version != preferred_version)
            {
                reporter.logger().operationDetail(QStringLiteral("Registration metrics version"),
                                                  QStringLiteral("Using settings_version %1 (requested %2)")
                                                      .arg(settings_version)
                                                      .arg(preferred_version));
            }

            std::vector<FrameId> frames = reg_group->lightFiles;
            std::sort(frames.begin(), frames.end());
            frames.erase(std::unique(frames.begin(), frames.end()), frames.end());

            std::optional<FrameId> reference_frame;
            const auto persisted_reference = metrics_reader.readReferenceFrameId(reg_group->groupKey, settings_version);
            if (persisted_reference.has_value())
            {
                reference_frame = stage_utils::persistedRegistrationFrameId(*persisted_reference);
            }
            if (!reference_frame.has_value())
            {
                reference_frame = reg_group->referenceFrame;
            }
            if (!reference_frame.has_value() && !frames.empty())
            {
                reference_frame = frames.front();
            }

            const auto records = loadFrameRecords(records_connection, reg_key, settings_version);
            if (records.empty())
            {
                reporter.logger().warning(
                    QStringLiteral("No registration metrics found for group '%1' at settings v%2. Skipping.")
                        .arg(group_label)
                        .arg(settings_version));
                return success_with_comet();
            }

            // Every light of the group, the file it is read from and its registration.
            struct FrameEntry
            {
                FrameId frame;
                QString inputPath;
                const FrameRecord* record = nullptr;
            };
            std::vector<FrameEntry> entries;
            entries.reserve(frames.size());
            for (const FrameId& frame : frames)
            {
                const auto it = records.find(frame.identityKey());
                // The file as it is now: this run's calibration and registration may have written it after the
                // execution plan was made.
                entries.push_back(
                    {frame, alignmentInputPathFor(manifest, *reg_group, frame),
                     it != records.end() && it->second.succeeded && it->second.hasTransform ? &it->second : nullptr});
            }
            QString reference_input_path;
            for (const FrameEntry& entry : entries)
            {
                if (reference_frame.has_value() && entry.frame == *reference_frame)
                {
                    reference_input_path = entry.inputPath;
                }
            }

            // A group whose lights the stack warps as it reads them writes nothing here, once the stacking stage has
            // confirmed every light it would read is registered as recorded.
            bool runtime_route_evaluated = false;
            bool materialized_outputs_required = false;
            for (const auto& integration_group : manifest.integrationGroups)
            {
                if (integration_group.registrationGroupKey.toString() != reg_key ||
                    ImageIntegration::usesCometOnlyStackingMode(integration_group.stacking.cometStackingMode) ||
                    !shouldConsiderIntegrationGroupForAlignment(manifest, integration_group, snapshot,
                                                                executionCounterAccumulator()))
                {
                    continue;
                }
                runtime_route_evaluated = true;
                const ImageIntegration::TransformOnReadRouting configured =
                    ImageIntegration::planLightsTransformOnRead(manifest, integration_group, snapshot);
                if (!configured.usesTransformOnRead())
                {
                    materialized_outputs_required = true;
                    LOG_INFO("LightsAlignment: group '{}' uses materialized registered images ({})",
                             integration_group.groupKey.toString().toStdString(), configured.reason.toStdString());
                    continue;
                }
                std::vector<ImageIntegration::RegisteredLightRecord> lights;
                for (const FrameId& frame : integration_group.lightFiles)
                {
                    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const FrameEntry& candidate) {
                        return candidate.frame == frame;
                    });
                    ImageIntegration::RegisteredLightRecord light;
                    light.frame = frame;
                    light.inputPath =
                        entry != entries.end() ? entry->inputPath : alignmentInputPathFor(manifest, *reg_group, frame);
                    if (entry != entries.end() && entry->record != nullptr)
                    {
                        light.transformJson = entry->record->transformJson;
                    }
                    lights.push_back(std::move(light));
                }
                const ImageIntegration::TransformOnReadRouting confirmed =
                    ImageIntegration::confirmLightsTransformOnRead(manifest, integration_group, lights,
                                                                   reference_input_path, snapshot);
                LOG_INFO("LightsAlignment: group '{}' runtime registration policy: {} ({})",
                         integration_group.groupKey.toString().toStdString(),
                         confirmed.usesTransformOnRead() ? "transform-on-read" : "materialized registered images",
                         confirmed.reason.toStdString());
                if (!confirmed.usesTransformOnRead())
                {
                    materialized_outputs_required = true;
                }
            }
            if (runtime_route_evaluated && !materialized_outputs_required)
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Registration input policy"),
                    QStringLiteral("Transform-on-read (persisted registration identities and transforms verified)"));
                return success_with_comet();
            }

            std::vector<const FrameEntry*> eligible;
            for (const FrameEntry& entry : entries)
            {
                if (entry.record != nullptr)
                {
                    eligible.push_back(&entry);
                }
            }
            if (eligible.empty())
            {
                reporter.logger().warning(QStringLiteral("No valid frames to align in group."));
                return success_with_comet();
            }

            const ImageIntegration::RegistrationCanvasMode canvas_mode =
                stage_utils::parseAlignmentMode(reg_group->settings.alignmentMode);
            const bool needs_consistent_canvas = canvas_mode != ImageIntegration::RegistrationCanvasMode::Standard;
            // Named from the files it warps, as they are now.
            const bool force_fits_outputs = registrationGroupMaterializationUsesFits(manifest, *reg_group);

            // Every eligible frame's header: its extent, for the plane, and its distortion, for a record that inherits
            // it.
            std::unordered_map<QString, stage_utils::RegistrationFrameFacts> facts_by_input;
            {
                an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
                if (!scope.hasValue())
                {
                    if (cancelledStatus(scope.status()))
                    {
                        return StageResult::cancelled();
                    }
                    return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                        scope.status(), IntegrationErrorCode::RegistrationFailed,
                        QStringLiteral("Alignment could not start"), reg_key));
                }
                for (const FrameEntry* entry : eligible)
                {
                    an::Result<stage_utils::RegistrationFrameFacts> facts =
                        stage_utils::probeRegistrationFrame(*scope, services->codecRegistry().get(), entry->inputPath);
                    if (facts.hasValue())
                    {
                        facts_by_input.emplace(entry->inputPath, std::move(*facts));
                    }
                    else if (cancelledStatus(facts.status()))
                    {
                        return StageResult::cancelled();
                    }
                }
            }

            std::vector<AlignmentWork> all_work;
            std::vector<stage_utils::RegistrationOutputPlaneFrame> plane_frames;
            std::optional<std::size_t> reference_index;
            int missing_output = 0;
            int up_to_date = 0;
            std::vector<bool> needs_processing;
            QStringList unreadable;
            for (const FrameEntry* entry : eligible)
            {
                const auto facts = facts_by_input.find(entry->inputPath);
                if (facts == facts_by_input.end())
                {
                    unreadable << entry->inputPath;
                    continue;
                }
                AlignmentWork item;
                item.frame = entry->frame;
                item.inputPath = entry->inputPath;
                item.transformJson = entry->record->transformJson;
                item.transform = entry->record->transform;
                item.distortion = resolvedDistortion(entry->record->distortionProvenance, facts->second.distortion);
                item.width = facts->second.width;
                item.height = facts->second.height;
                item.outputPath = registeredPathForAlignmentInput(manifest.outputDirectory, reg_group->groupKey,
                                                                  entry->inputPath, force_fits_outputs);
                bool current = false;
                if (!manifest.reprocessExisting)
                {
                    const QString out = entry->record->outputPath.trimmed();
                    current = !out.isEmpty() && sameCleanAbsolutePath(out, item.outputPath) &&
                              registeredOutputMatchesRevision(out, entry->record->outputSizeBytes,
                                                              entry->record->outputModifiedAtMs,
                                                              entry->record->outputIdentityToken);
                    current ? ++up_to_date : ++missing_output;
                }
                if (reference_frame.has_value() && entry->frame == *reference_frame)
                {
                    reference_index = plane_frames.size();
                }
                plane_frames.push_back({.transform = item.transform,
                                        .width = item.width,
                                        .height = item.height,
                                        .distortion = item.distortion});
                needs_processing.push_back(manifest.reprocessExisting || !current);
                all_work.push_back(std::move(item));
            }
            for (const QString& path : unreadable)
            {
                reporter.logger().warning(
                    QStringLiteral("Failed to read the header of %1; it is not aligned").arg(path));
            }
            if (all_work.empty())
            {
                return StageResult::failure(IntegrationError(IntegrationErrorCode::RegistrationFailed,
                                                             QStringLiteral("Lights alignment produced no registered "
                                                                            "outputs"),
                                                             {},
                                                             QStringLiteral("No registered light of group '%1' "
                                                                            "could be read.")
                                                                 .arg(group_label)));
            }
            if (!manifest.reprocessExisting && missing_output == 0)
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Incremental"),
                    QStringLiteral("All %1 eligible frame(s) already aligned; skipping.").arg(all_work.size()));
                return success_with_comet();
            }
            reporter.logger().operationDetail(
                QStringLiteral("Alignment reason"),
                manifest.reprocessExisting
                    ? QStringLiteral("Reprocess existing enabled (%1 eligible frame(s)).").arg(all_work.size())
                    : QStringLiteral("%1 missing or stale output, %2 already up-to-date")
                          .arg(missing_output)
                          .arg(up_to_date));

            // The plane: the reference frame's for the standard canvas, every frame's union for a mosaic, what every
            // frame covers for an intersection - and the reference's when nothing is covered by all of them.
            const std::size_t plane_reference = reference_index.value_or(0u);
            const an::Result<stage_utils::PlannedRegistrationOutputPlane> plane =
                stage_utils::planRegistrationOutputPlane(plane_frames, plane_reference, canvas_mode);
            if (!plane.hasValue())
            {
                return StageResult::failure(ImageIntegration::integrationErrorFromEngine(
                    plane.status(), IntegrationErrorCode::RegistrationFailed,
                    QStringLiteral("The plane to align onto could not be planned"), reg_key));
            }
            if (plane->intersectionReplaced)
            {
                reporter.logger().warning(stage_utils::intersectionReplacedWarning(group_label));
            }
            const ImageIntegration::RegistrationCanvasMode used_mode = plane->usedMode;
            const an::RegistrationCanvas& canvas = plane->canvas;

            // The plane's solution: the record the registration stage wrote when it still describes this plane, and
            // otherwise the reference frame's own solution moved onto it, which is then recorded.
            std::optional<an::WcsSolution> plane_solution;
            QString why_unsolved;
            QString expectation_error;
            const auto expectations = stage_utils::computeRegistrationOutputPlaneExpectations(
                plane_frames, plane_reference, used_mode, expectation_error);
            const std::optional<QString> stored_json =
                metrics_reader.readOutputWcsJson(reg_group->groupKey, settings_version);
            if (stored_json.has_value() && !stored_json->trimmed().isEmpty() && expectations.has_value())
            {
                std::vector<an::CodecCard> cards;
                stage_utils::OutputPlaneWcsDescriptorInfo info;
                QString descriptor_error;
                if (stage_utils::parseOutputPlaneWcsJson(*stored_json, &cards, &info, descriptor_error) &&
                    stage_utils::validateOutputPlaneWcsDescriptor(info, persisted_reference.value_or(QString()),
                                                                  stage_utils::alignmentModeKey(used_mode),
                                                                  expectations->width, expectations->height,
                                                                  expectations->sipCorrectionApplied, descriptor_error))
                {
                    an::Result<an::WcsSolution> solution =
                        an::ui::readImageWcsSolution(cards, canvas.width, canvas.height);
                    if (solution.hasValue())
                    {
                        plane_solution = std::move(*solution);
                    }
                    else
                    {
                        why_unsolved = ImageIntegration::engineFailureText(solution.status());
                    }
                }
                else
                {
                    why_unsolved = descriptor_error;
                }
            }
            if (!plane_solution.has_value() && reference_index.has_value())
            {
                const auto facts = facts_by_input.find(all_work[*reference_index].inputPath);
                if (facts != facts_by_input.end() && facts->second.solution.has_value())
                {
                    an::Result<an::WcsSolution> moved = stage_utils::registrationOutputPlaneSolution(
                        *facts->second.solution, canvas, all_work[*reference_index].distortion.has_value());
                    an::Result<std::vector<an::CodecCard>> cards =
                        moved.hasValue() ? an::buildWcsCards(*moved)
                                         : an::Result<std::vector<an::CodecCard>>(moved.status());
                    if (cards.hasValue())
                    {
                        plane_solution = *moved;
                        stage_utils::OutputPlaneWcsDescriptorInfo info;
                        info.sourceKind = QStringLiteral("reference_frame");
                        info.sourceFrameId = reference_frame->canonicalPath();
                        info.alignmentMode = stage_utils::alignmentModeKey(used_mode);
                        info.width = static_cast<int>(canvas.width);
                        info.height = static_cast<int>(canvas.height);
                        info.sipCorrectionApplied = expectations.has_value() && expectations->sipCorrectionApplied;
                        if (std::optional<an::ui::RegistrationGroupState> state =
                                state_loader.loadGroupState(reg_key, settings_version);
                            state.has_value())
                        {
                            state->outputWcsJson = stage_utils::serializeOutputPlaneWcsJson(*cards, info);
                            if (!metrics_writer.upsertGroupState(*state))
                            {
                                reporter.logger().warning(
                                    QStringLiteral("Failed to record the output plane's solution for %1")
                                        .arg(group_label));
                            }
                        }
                        reporter.logger().operationDetail(QStringLiteral("Output-plane WCS"),
                                                          QStringLiteral("refreshed for the %1 canvas %2x%3")
                                                              .arg(info.alignmentMode)
                                                              .arg(info.width)
                                                              .arg(info.height));
                    }
                }
            }
            if (plane_solution.has_value())
            {
                reporter.logger().operationDetail(
                    QStringLiteral("Output-plane WCS"),
                    QStringLiteral("%1x%2").arg(plane_solution->imageWidth()).arg(plane_solution->imageHeight()));
                if (std::any_of(all_work.begin(), all_work.end(), [](const AlignmentWork& item) {
                        return !stage_utils::supportsEmbeddedWcsOutput(item.outputPath);
                    }))
                {
                    reporter.logger().warning(
                        QStringLiteral("Aligned WCS is not embedded for non-FITS/XISF outputs in %1; downstream "
                                       "stacking will use the persisted registration output-plane descriptor.")
                            .arg(group_label));
                }
            }
            else if (!why_unsolved.isEmpty())
            {
                reporter.logger().warning(QStringLiteral("Failed to use the persisted output-plane WCS for %1: %2")
                                              .arg(group_label, why_unsolved));
            }

            // A consistent canvas aligns every frame again when any output is missing, so every registered light has
            // the plane's size; the standard canvas aligns only what is missing.
            std::vector<AlignmentWork> work;
            for (std::size_t index = 0; index < all_work.size(); ++index)
            {
                if (needs_consistent_canvas || needs_processing[index])
                {
                    work.push_back(all_work[index]);
                }
            }

            const an::Result<an::KernelInterpolation> interpolation =
                an::ui::processes::alignmentInterpolationOf(reg_group->settings.interpolation);
            const an::KernelInterpolation sampling =
                interpolation.hasValue() ? *interpolation : an::KernelInterpolation::Lanczos4;
            reporter.logger().operationDetail(
                QStringLiteral("Interpolation"),
                reg_group->settings.interpolation.trimmed().compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                    ? QStringLiteral("%1 (Auto)").arg(QString::fromLatin1(interpolationName(sampling)))
                    : QString::fromLatin1(interpolationName(sampling)));
            reporter.logger().operationDetail(QStringLiteral("Aligning %1 frames").arg(work.size()),
                                              QStringLiteral("onto %1x%2 at (%3, %4)")
                                                  .arg(canvas.width)
                                                  .arg(canvas.height)
                                                  .arg(canvas.origin.x, 0, 'f', 2)
                                                  .arg(canvas.origin.y, 0, 'f', 2));

            const an::ui::processes::RegistrationDemosaic demosaic =
                an::ui::processes::registrationDemosaicOf(reg_group->settings);
            const int total = static_cast<int>(work.size());
            const AlignmentBatch batch =
                alignFrames(*services, manifest, masters, snapshot, work, canvas, sampling, plane_solution, demosaic,
                            QStringLiteral("registered"), token, [&](const int done, const QString& file) {
                                stage_utils::reportStageProgress(
                                    reporter, stageId(), group_index, total_groups,
                                    static_cast<float>(done) / static_cast<float>((std::max)(1, total)),
                                    QStringLiteral("Aligning image %1 of %2").arg(done).arg(total), file);
                            });
            if (batch.cancelled || token.isCancellationRequested())
            {
                return StageResult::cancelled();
            }
            for (const QString& failure : batch.failures)
            {
                reporter.logger().warning(QStringLiteral("Alignment failed for %1").arg(failure));
            }
            if (batch.succeeded == 0)
            {
                QString details = QStringLiteral("Alignment for group '%1' produced no registered output files "
                                                 "(total=%2, failed=%3).")
                                      .arg(group_label)
                                      .arg(work.size())
                                      .arg(batch.failures.size());
                if (!batch.failures.isEmpty())
                {
                    details += QStringLiteral(" First failure(s): %1")
                                   .arg(batch.failures.mid(0, 5).join(QStringLiteral("; ")));
                }
                LOG_ERROR("LightsAlignmentStage: {}", details.toStdString());
                return StageResult::failure(
                    IntegrationError(IntegrationErrorCode::RegistrationFailed,
                                     QStringLiteral("Lights alignment produced no registered outputs"), {}, details));
            }

            std::vector<an::ui::RegistrationAlignedOutputPathUpdate> updates;
            updates.reserve(batch.outputs.size());
            for (const auto& [frame, revision] : batch.outputs)
            {
                updates.push_back(
                    {frame, revision.path, revision.sizeBytes, revision.modifiedAtMs, revision.identityToken});
            }
            QString update_error;
            if (!metrics_writer.updateAlignedOutputPathsForGroup(reg_key, settings_version, updates, &update_error))
            {
                const QString details =
                    QStringLiteral("Alignment produced %1 registered output file(s) for %2, but no output paths were "
                                   "persisted to registration metrics. Batch error: %3")
                        .arg(updates.size())
                        .arg(group_label, update_error);
                LOG_ERROR("LightsAlignmentStage: {}", details.toStdString());
                return StageResult::failure(IntegrationError(
                    IntegrationErrorCode::RegistrationFailed,
                    QStringLiteral("Lights alignment did not persist registered output paths"), {}, details));
            }

            StageResult result = success_with_comet();
            if (!batch.failures.isEmpty())
            {
                IntegrationErrorReport report;
                for (const QString& failure : batch.failures)
                {
                    report.addError(IntegrationError::registrationFailed(
                        QStringLiteral("Frame alignment failed: %1").arg(failure)));
                }
                StageResult partial = StageResult::partialSuccess(report);
                partial.alignmentGroupResults = std::move(result.alignmentGroupResults);
                result = std::move(partial);
            }
            AlignmentGroupResult group_result;
            group_result.groupKey = reg_key;
            group_result.totalAligned = batch.succeeded;
            group_result.sipCorrectedCount = batch.sipCorrected;
            result.alignmentGroupResults.push_back(std::move(group_result));
            return result;
        };

        const StageResult group_result = process_group();
        for (const auto& alignment_result : group_result.alignmentGroupResults)
        {
            all_alignment_results.push_back(alignment_result);
        }
        if (group_result.isCancelled())
        {
            return StageResult::cancelled();
        }
        if (group_result.isFailure())
        {
            ++failed_groups;
            error_report.addError(group_result.error);
            reporter.logger().warning(QStringLiteral("Group '%1' failed: %2 - continuing with remaining groups.")
                                          .arg(group_label, group_result.error.toUserString()));
        }
        else if (group_result.isPartialSuccess())
        {
            ++successful_groups;
            error_report.merge(group_result.errorReport);
        }
        else
        {
            ++successful_groups;
        }
        reporter.logger().groupCompleted(group_index, total_groups, group_label, group_timer.elapsed());
    }

    // What the calibrations of the lights read in memory fell back to, once per calibration.
    QStringList calibration_warnings;
    stage_utils::sayStageWarnings(reporter, masters.fallbackWarnings(), calibration_warnings);
    reporter.logger().stageCompleted(name(), total_groups, stage_timer.elapsed());
    if (!error_report.errors.empty())
    {
        if (successful_groups == 0 && failed_groups > 0)
        {
            StageResult failure = StageResult::failure(error_report.errors.front());
            failure.warnings = calibration_warnings;
            return failure;
        }
        auto result = StageResult::partialSuccess(error_report);
        result.warnings.append(calibration_warnings);
        result.alignmentGroupResults = std::move(all_alignment_results);
        return result;
    }
    StageResult result = StageResult::success();
    result.warnings = calibration_warnings;
    result.alignmentGroupResults = std::move(all_alignment_results);
    return result;
}
