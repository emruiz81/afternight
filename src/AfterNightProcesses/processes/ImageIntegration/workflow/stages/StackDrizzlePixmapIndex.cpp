// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A group's drizzle descriptors as the drizzle stage writes them and the stack reads them.

#include "stdafx.hpp"
#include "StackDrizzlePixmapIndex.h"

#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationEngineError.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsReader.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleModel.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>

namespace ImageIntegration
{

QString drizzleDescriptorDirectory(const WorkflowManifest& manifest, const WorkflowManifest::IntegrationGroup& group)
{
    const QString configured = group.drizzlePixmapArtifactDir.value_or(QString()).trimmed();
    if (!configured.isEmpty())
    {
        return configured;
    }
    if (manifest.outputDirectory.trimmed().isEmpty())
    {
        return {};
    }
    return QDir(manifest.outputDirectory)
        .filePath(QStringLiteral(".afternight/image_integration/artifacts/drizzle/%1/pixmaps")
                      .arg(sanitizeFilename(group.groupKey.toString())));
}

QString drizzleGroupSidecarPath(const QString& directory)
{
    return QDir(directory).filePath(QStringLiteral(".meta.json"));
}

an::Result<an::DrizzleMapDescriptor> drizzleMapDescriptorFor(const std::uint32_t sourceWidth,
                                                             const std::uint32_t sourceHeight,
                                                             const std::uint32_t outputWidth,
                                                             const std::uint32_t outputHeight, const double scale,
                                                             const an::DrizzleMappingRequest& mapping)
{
    const std::optional<an::ImageExtent> input =
        an::ImageExtent::create(sourceWidth, sourceHeight, 1u, an::defaultEngineLimits());
    const std::optional<an::ImageExtent> output =
        an::ImageExtent::create(outputWidth, outputHeight, 1u, an::defaultEngineLimits());
    if (!input.has_value() || !output.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the light's or the canvas's extent is invalid");
    }
    const std::optional<an::ImageDescriptor> image =
        an::ImageDescriptor::create({.extent = *input,
                                     .sampleType = an::SampleType::Float32,
                                     .channelSchema = an::ChannelSchema::Mono,
                                     .colorSpace = an::ColorSpace::Unspecified,
                                     .numericDomain = an::NumericDomain::NormalizedLinear,
                                     .cfa = {},
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0});
    if (!image.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the light cannot be described");
    }
    an::DrizzleRequest request{.input = *image, .frameCount = 1u};
    request.parameters.scale = scale;
    request.output.extent = *output;
    const an::Result<an::DrizzleModel> model = an::DrizzleModel::create(request);
    if (!model.hasValue())
    {
        return model.status();
    }
    return an::DrizzleMapDescriptor::create(*model, mapping);
}

an::Result<an::DrizzleMapDescriptor> drizzleMapDescriptorOf(const DrizzleDescriptorDocument& document)
{
    return drizzleMapDescriptorFor(document.sourceWidth, document.sourceHeight, document.outputWidth,
                                   document.outputHeight, document.scale, drizzleMappingRequestOf(document));
}

an::Result<QByteArray> drizzleEngineIdentityOf(const an::DrizzleMapDescriptor& descriptor,
                                               const an::DrizzleNormalizationSpace space)
{
    const an::Result<an::DrizzleMapIdentity> identity = an::encodeDrizzleMapIdentity(descriptor, space);
    if (!identity.hasValue())
    {
        return identity.status();
    }
    QByteArray bytes(static_cast<qsizetype>(identity->bytes.size()), Qt::Uninitialized);
    std::memcpy(bytes.data(), identity->bytes.data(), identity->bytes.size());
    return bytes;
}

DrizzleDescriptorIndexRead readDrizzleDescriptorIndex(const QString& directory,
                                                      const WorkflowManifest::IntegrationGroup& group)
{
    DrizzleDescriptorIndexRead result;
    const QString sidecarPath = drizzleGroupSidecarPath(directory);
    QFile file(sidecarPath);
    if (!file.exists())
    {
        result.reason = QStringLiteral("No drizzle descriptors were written for this group.");
        return result;
    }
    QByteArray bytes;
    QString error;
    if (!schema::readMetadataSidecarBounded(file, bytes, &error))
    {
        result.reason = error;
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        result.reason = QStringLiteral("The drizzle sidecar is not readable.");
        return result;
    }
    DrizzleDescriptorIndex index;
    index.sidecar = json.object();
    const int schemaVersion = index.sidecar.value(QStringLiteral("schemaVersion")).toInt();
    if (schemaVersion > schema::kDrizzleGroupSidecarSchemaVersion)
    {
        result.newerSchema = true;
        result.reason =
            QStringLiteral("The drizzle sidecar was written by a newer version (schema %1).").arg(schemaVersion);
        return result;
    }
    if (schemaVersion != schema::kDrizzleGroupSidecarSchemaVersion ||
        !schema::statesCurrentArtifactFamily(index.sidecar, schema::ArtifactFamily::DrizzleDescriptor))
    {
        result.reason = QStringLiteral("The drizzle sidecar is another generation's.");
        return result;
    }
    if (index.sidecar.value(QStringLiteral("stageId")).toString() != QLatin1String(kDrizzleStageId) ||
        index.sidecar.value(QStringLiteral("integrationGroupKey")).toString() != group.groupKey.toString())
    {
        result.reason = QStringLiteral("The drizzle sidecar belongs to another stage or group.");
        return result;
    }
    const int outputWidth = index.sidecar.value(QStringLiteral("outputWidth")).toInt();
    const int outputHeight = index.sidecar.value(QStringLiteral("outputHeight")).toInt();
    if (outputWidth <= 0 || outputHeight <= 0)
    {
        result.reason = QStringLiteral("The drizzle sidecar's output extent is invalid.");
        return result;
    }
    index.outputWidth = static_cast<std::uint32_t>(outputWidth);
    index.outputHeight = static_cast<std::uint32_t>(outputHeight);

    const QDir root(directory);
    for (const auto& entry : index.sidecar.value(QStringLiteral("frames")).toArray())
    {
        const QJsonObject frame = entry.toObject();
        QString frameKey;
        try
        {
            const FrameId frameId = FrameId::fromJson(frame.value(QStringLiteral("frameId")));
            if (!frameId.isValid())
            {
                throw std::invalid_argument("invalid frame");
            }
            frameKey = frameId.identityKey();
        }
        catch (...)
        {
            result.reason = QStringLiteral("The drizzle sidecar names a light that is not valid.");
            return result;
        }
        const QString descriptorFile = frame.value(QStringLiteral("descriptorFile")).toString();
        if (descriptorFile.trimmed().isEmpty() || descriptorFile.contains(QLatin1Char('/')) ||
            descriptorFile.contains(QLatin1Char('\\')) || index.byFrame.contains(frameKey))
        {
            result.reason = QStringLiteral("The drizzle sidecar's light list is invalid.");
            return result;
        }
        DrizzleDescriptorRecord record;
        record.descriptorPath = root.filePath(descriptorFile);
        auto document = readDrizzleDescriptor(record.descriptorPath, error);
        if (!document.has_value())
        {
            result.reason = QStringLiteral("%1: %2").arg(descriptorFile, error);
            return result;
        }
        if (FrameId(document->frameId).identityKey() != frameKey || document->outputWidth != index.outputWidth ||
            document->outputHeight != index.outputHeight || document->engineIdentity.isEmpty())
        {
            result.reason =
                QStringLiteral("%1 does not describe the light and canvas its sidecar names.").arg(descriptorFile);
            return result;
        }
        const an::Result<an::DrizzleMapDescriptor> descriptor = drizzleMapDescriptorOf(*document);
        if (!descriptor.hasValue())
        {
            result.reason =
                QStringLiteral("%1: %2").arg(descriptorFile, QString::fromUtf8(descriptor.status().message()));
            return result;
        }
        const an::Status valid = an::validateDrizzleMapIdentity(
            *descriptor, document->normalizationSpace,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(document->engineIdentity.constData()),
                                       static_cast<std::size_t>(document->engineIdentity.size())));
        if (valid.failed())
        {
            result.reason = QStringLiteral("%1 does not hold the engine's identity of its own geometry: %2")
                                .arg(descriptorFile, QString::fromUtf8(valid.message()));
            return result;
        }
        if (document->densePayload.has_value())
        {
            record.densePayloadPath = root.filePath(document->densePayload->fileName);
            const QFileInfo dense(record.densePayloadPath);
            if (!dense.isFile() || static_cast<std::uint64_t>(dense.size()) != document->densePayload->length)
            {
                result.reason =
                    QStringLiteral("%1's dense payload is missing or not its stated length.").arg(descriptorFile);
                return result;
            }
        }
        record.document = std::move(*document);
        index.byFrame.emplace(frameKey, std::move(record));
    }
    result.index = std::move(index);
    return result;
}

namespace
{

[[nodiscard]] std::optional<an::SipDistortion>
resolvedDistortionOf(const stage_utils::SourceDistortionProvenance& provenance,
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

[[nodiscard]] bool cancelledStatus(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

[[nodiscard]] DrizzleGeometryResolution failedWith(IntegrationError error)
{
    DrizzleGeometryResolution resolution;
    resolution.error = std::move(error);
    return resolution;
}

} // namespace

std::optional<an::ui::CometStackingProductResolveRequest>
cometProductRequestFor(const QString& metricsPath, const WorkflowManifest::IntegrationGroup& group,
                       const std::optional<int> settingsVersion, const bool drizzle,
                       WorkflowExecutionCounterAccumulator* counters, QString& error)
{
    an::ui::RegistrationMetricsStateLoader loader(metricsPath, QStringLiteral("CometProductStateLoader"), counters);
    if (!loader.open(&error))
    {
        error = QStringLiteral("The registration database could not be opened for the comet products: %1").arg(error);
        return std::nullopt;
    }
    QString loadError;
    const auto state = settingsVersion.has_value()
                           ? loader.loadGroupState(group.registrationGroupKey.toString(), *settingsVersion, &loadError)
                           : loader.loadLatestGroupState(group.registrationGroupKey.toString(), &loadError);
    if (!state.has_value())
    {
        error = loadError.trimmed().isEmpty()
                    ? QStringLiteral("The registration state the comet products belong to was not found.")
                    : loadError;
        return std::nullopt;
    }
    an::ui::CometStackingProductResolveRequest request;
    request.databasePath = metricsPath;
    request.groupKey = state->groupKey;
    request.registrationHash = state->registrationHash;
    request.lightFilesHash = state->lightFilesHash;
    request.calibrationHash = state->calibrationHash;
    request.resolvedReferenceFrameId = state->referenceFrameId;
    request.stackingMode = group.stacking.cometStackingMode;
    request.role = QStringLiteral("comet_aligned");
    request.drizzle = drizzle;
    request.frameIds = group.lightFiles;
    request.counters = counters;
    return request;
}

QString fingerprintCometProducts(const an::ui::CometStackingProductResolveResult& products)
{
    QJsonObject object;
    object.insert(QStringLiteral("cometRunId"), products.cometRunId);
    object.insert(QStringLiteral("role"), products.role);
    object.insert(QStringLiteral("kind"), static_cast<int>(products.kind));
    if (products.outputPlane.has_value())
    {
        const auto& plane = *products.outputPlane;
        QJsonObject planeObject;
        planeObject.insert(QStringLiteral("id"), plane.cometOutputPlaneId);
        planeObject.insert(QStringLiteral("canvasWidth"), plane.canvasWidth);
        planeObject.insert(QStringLiteral("canvasHeight"), plane.canvasHeight);
        planeObject.insert(QStringLiteral("canvasOffsetX"), plane.canvasOffsetX);
        planeObject.insert(QStringLiteral("canvasOffsetY"), plane.canvasOffsetY);
        planeObject.insert(QStringLiteral("sourceFingerprint"), plane.sourceFingerprint);
        planeObject.insert(QStringLiteral("updatedAt"), QString::number(plane.updatedAt));
        object.insert(QStringLiteral("outputPlane"), planeObject);
    }
    QJsonArray transforms;
    for (const auto& transform : products.transforms)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("frameId"), transform.frameId.canonicalPath());
        entry.insert(QStringLiteral("transformJson"), transform.transformJson);
        entry.insert(QStringLiteral("sourceDistortionJson"), transform.sourceDistortionJson);
        entry.insert(QStringLiteral("succeeded"), transform.succeeded);
        entry.insert(QStringLiteral("updatedAt"), QString::number(transform.updatedAt));
        transforms.append(entry);
    }
    object.insert(QStringLiteral("transforms"), transforms);
    QJsonArray outputs;
    for (const auto& output : products.registeredOutputs)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("frameId"), output.frameId.canonicalPath());
        entry.insert(QStringLiteral("outputPath"), output.outputPath);
        entry.insert(QStringLiteral("artifactHash"), output.artifactHash);
        entry.insert(QStringLiteral("succeeded"), output.succeeded);
        entry.insert(QStringLiteral("updatedAt"), QString::number(output.updatedAt));
        outputs.append(entry);
    }
    object.insert(QStringLiteral("registeredOutputs"), outputs);
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
}

DrizzleGeometryResolution resolveDrizzleGroupGeometry(const WorkflowStageServices& services,
                                                      const WorkflowManifest& manifest,
                                                      const WorkflowManifest::IntegrationGroup& group,
                                                      WorkflowExecutionCounterAccumulator* counters)
{
    if (!group.drizzle.has_value())
    {
        return failedWith(
            IntegrationError(IntegrationErrorCode::InvalidConfiguration, QStringLiteral("The group does not drizzle")));
    }
    const auto& drizzle = *group.drizzle;
    // The pattern a filter-array drizzle states reads every light as that array's samples, as the stack reads them;
    // left to the lights, each must state its own.
    std::optional<an::CfaPattern> statedPattern;
    if (drizzle.cfaAware && !stage_utils::parseBayerPattern(drizzle.cfaPattern, statedPattern))
    {
        return failedWith(IntegrationError(IntegrationErrorCode::InvalidConfiguration,
                                           QStringLiteral("Drizzle CFA pattern is invalid"), {}, drizzle.cfaPattern));
    }
    const QString metricsPath = QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    if (!QFileInfo::exists(metricsPath))
    {
        return failedWith(IntegrationError::fileNotFound(metricsPath));
    }
    stage_utils::RegistrationMetricsReader reader(metricsPath, QStringLiteral("DrizzleGeometry"), counters);
    QString error;
    if (!reader.open(&error))
    {
        return failedWith(IntegrationError::fileNotReadable(
            metricsPath, error.isEmpty() ? QStringLiteral("Failed to open registration metrics database") : error));
    }
    const QString registrationKey = group.registrationGroupKey.toString();
    const auto registrationGroup =
        std::find_if(manifest.registrationGroups.begin(), manifest.registrationGroups.end(),
                     [&](const auto& candidate) { return candidate.groupKey == group.registrationGroupKey; });
    const WorkflowManifest::RegistrationGroup* registration =
        registrationGroup == manifest.registrationGroups.end() ? nullptr : &*registrationGroup;
    const std::optional<int> settingsVersion = reader.resolveSettingsVersionForGroup(
        registrationKey, registration != nullptr ? std::optional<int>(registration->settings.version) : std::nullopt);
    if (!settingsVersion.has_value())
    {
        return failedWith(IntegrationError(IntegrationErrorCode::MissingRequiredFrames,
                                           QStringLiteral("Registration settings version not found"), {},
                                           registrationKey));
    }

    DrizzleGroupGeometry geometry;
    geometry.settingsVersion = *settingsVersion;
    std::unordered_map<QString, an::RegistrationTransform> transforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
    std::optional<an::ui::CometOutputPlaneRecord> cometPlane;
    if (usesCometStackingMode(group.stacking.cometStackingMode))
    {
        auto request = cometProductRequestFor(metricsPath, group, settingsVersion, true, counters, error);
        if (!request.has_value())
        {
            return failedWith(IntegrationError(IntegrationErrorCode::MissingRequiredFrames, error));
        }
        if (usesStarsPlusCometExperimentalStackingMode(group.stacking.cometStackingMode))
        {
            const auto dual = an::ui::resolveStarsPlusCometExperimentalProducts(*request);
            if (!dual.available)
            {
                return failedWith(IntegrationError(
                    IntegrationErrorCode::MissingRequiredFrames,
                    dual.errorMessage.trimmed().isEmpty()
                        ? QStringLiteral("%1 drizzle requires matching star base and pure comet products.")
                              .arg(starsPlusCometExperimentalStackingModeToken())
                        : dual.errorMessage));
            }
            request->stackingMode = QStringLiteral("Comet");
        }
        const auto products = an::ui::resolveCometStackingProducts(*request);
        if (!products.available || products.kind != an::ui::CometStackingProductKind::TransformRoles ||
            !products.outputPlane.has_value() || products.outputPlane->canvasWidth <= 0 ||
            products.outputPlane->canvasHeight <= 0)
        {
            return failedWith(IntegrationError(
                IntegrationErrorCode::MissingRequiredFrames,
                products.errorMessage.trimmed().isEmpty()
                    ? QStringLiteral("Comet drizzle requires comet_aligned transform products and their output plane.")
                    : products.errorMessage));
        }
        for (const auto& record : products.transforms)
        {
            an::RegistrationTransform transform;
            stage_utils::SourceDistortionProvenance recordProvenance;
            QString parseError;
            if (!record.succeeded || !stage_utils::parseRegistrationTransformJson(record.transformJson, transform,
                                                                                  &recordProvenance, parseError))
            {
                continue;
            }
            if (!record.sourceDistortionJson.trimmed().isEmpty())
            {
                const QJsonDocument json = QJsonDocument::fromJson(record.sourceDistortionJson.toUtf8());
                an::SipDistortion model;
                if (!json.isObject() || !stage_utils::sipDistortionFromJson(json.object(), model, &parseError))
                {
                    return failedWith(IntegrationError::registrationFailed(
                        QStringLiteral("The comet source distortion of %1 is invalid: %2")
                            .arg(record.frameId.canonicalPath(), parseError)));
                }
                recordProvenance = stage_utils::SourceDistortionProvenance::explicitModel(model);
            }
            transforms.insert_or_assign(record.frameId.identityKey(), transform);
            provenance.insert_or_assign(record.frameId.identityKey(), recordProvenance);
        }
        cometPlane = products.outputPlane;
        geometry.cometProductsFingerprint = fingerprintCometProducts(products);
        geometry.cometRunId = products.cometRunId;
        geometry.cometRole = products.role;
        geometry.cometOutputPlaneId = products.outputPlane->cometOutputPlaneId;
    }
    else
    {
        if (!reader.loadTransforms(registrationKey, *settingsVersion, transforms, error, &provenance) ||
            !stage_utils::rekeyRegistrationFrameLookupByIdentity(transforms, &error) ||
            !stage_utils::rekeyRegistrationFrameLookupByIdentity(provenance, &error))
        {
            return failedWith(IntegrationError::registrationFailed(
                error.isEmpty() ? QStringLiteral("Failed to load registration transforms") : error));
        }
    }

    std::optional<FrameId> reference;
    if (const auto persisted = reader.readReferenceFrameId(group.registrationGroupKey, *settingsVersion))
    {
        reference = stage_utils::persistedRegistrationFrameId(*persisted);
    }
    if (!reference.has_value() && registration != nullptr)
    {
        reference = registration->referenceFrame;
    }

    an::Result<an::consumer::OperationScope> scope = services.admitStageOperation();
    if (!scope.hasValue())
    {
        DrizzleGeometryResolution resolution;
        resolution.cancelled = cancelledStatus(scope.status());
        resolution.error = integrationErrorFromEngine(scope.status(), IntegrationErrorCode::DrizzleFailed,
                                                      QStringLiteral("The drizzle geometry could not be resolved"));
        return resolution;
    }
    for (const FrameId& light : group.lightFiles)
    {
        const auto transform = transforms.find(light.identityKey());
        if (transform == transforms.end())
        {
            geometry.withoutTransform << light.canonicalPath();
            continue;
        }
        const an::Result<stage_utils::RegistrationFrameFacts> facts =
            stage_utils::probeRegistrationFrame(*scope, services.codecRegistry().get(), light.canonicalPath());
        if (!facts.hasValue())
        {
            DrizzleGeometryResolution resolution;
            resolution.cancelled = cancelledStatus(facts.status());
            resolution.error =
                integrationErrorFromEngine(facts.status(), IntegrationErrorCode::FileNotReadable,
                                           QStringLiteral("A light's header could not be read"), light.canonicalPath());
            return resolution;
        }
        DrizzleFrameGeometry frame;
        frame.frame = light;
        // The file as it is now: this run's calibration and registration may have written it since the execution
        // plan was made.
        frame.inputPath = registrationInputPathForFrame(manifest.outputDirectory, manifest.calibrateLightsInMemory,
                                                        registration, light);
        frame.transform = transform->second;
        const auto recorded = provenance.find(light.identityKey());
        frame.provenance = recorded != provenance.end() ? recorded->second
                                                        : stage_utils::SourceDistortionProvenance::inheritMetadata();
        frame.resolvedDistortion = resolvedDistortionOf(frame.provenance, facts->distortion);
        frame.width = facts->width;
        frame.height = facts->height;
        frame.channels = facts->channels;
        frame.mosaic = facts->mosaic;
        if (drizzle.cfaAware && frame.channels != 1u)
        {
            return failedWith(IntegrationError(IntegrationErrorCode::IncompatibleChannels,
                                               QStringLiteral("CFA-aware drizzle requires single-channel input"),
                                               light.canonicalPath()));
        }
        if (drizzle.cfaAware && !frame.mosaic && !statedPattern.has_value())
        {
            return failedWith(IntegrationError(
                IntegrationErrorCode::IncompatibleChannels,
                QStringLiteral("CFA-aware drizzle requires each light's filter array: this light's header has no "
                               "BAYERPAT card, and the drizzle's CFA pattern is Auto, which reads it from the header"),
                light.canonicalPath()));
        }
        if (reference.has_value() && light.identityKey() == reference->identityKey())
        {
            geometry.referenceIndex = geometry.frames.size();
        }
        geometry.frames.push_back(std::move(frame));
    }
    if (geometry.frames.empty())
    {
        return failedWith(IntegrationError::registrationFailed(QStringLiteral(
            "No drizzle descriptors can be written because every light lacks a registration transform")));
    }

    // The comet's plane when the lights are comet-aligned; otherwise every light's rectangle on the group's canvas
    // mode, the standard canvas in place of an intersection no whole output pixel is common to.
    const ImageIntegration::RegistrationCanvasMode mode =
        registration != nullptr ? stage_utils::parseAlignmentMode(registration->settings.alignmentMode)
                                : ImageIntegration::RegistrationCanvasMode::Standard;
    an::Result<stage_utils::PlannedDrizzleCanvas> canvas = [&]() -> an::Result<stage_utils::PlannedDrizzleCanvas> {
        if (cometPlane.has_value())
        {
            const an::Result<an::DrizzleCanvas> onPlane = an::planDrizzleCanvasOnPlane(
                an::RegistrationBounds{.minimumX = cometPlane->canvasOffsetX,
                                       .minimumY = cometPlane->canvasOffsetY,
                                       .maximumX = cometPlane->canvasOffsetX + cometPlane->canvasWidth,
                                       .maximumY = cometPlane->canvasOffsetY + cometPlane->canvasHeight},
                static_cast<double>(drizzle.scale));
            if (!onPlane.hasValue())
            {
                return onPlane.status();
            }
            return stage_utils::PlannedDrizzleCanvas{.canvas = *onPlane, .usedMode = mode};
        }
        std::vector<stage_utils::RegistrationOutputPlaneFrame> planeFrames;
        planeFrames.reserve(geometry.frames.size());
        for (const DrizzleFrameGeometry& frame : geometry.frames)
        {
            planeFrames.push_back({frame.transform, frame.width, frame.height, frame.resolvedDistortion});
        }
        return stage_utils::planDrizzleOutputPlane(planeFrames, geometry.referenceIndex, mode,
                                                   static_cast<double>(drizzle.scale),
                                                   static_cast<double>(drizzle.pixelFraction));
    }();
    if (!canvas.hasValue())
    {
        DrizzleGeometryResolution resolution;
        resolution.error = integrationErrorFromEngine(canvas.status(), IntegrationErrorCode::DrizzleFailed,
                                                      QStringLiteral("The drizzle canvas could not be planned"));
        return resolution;
    }
    geometry.canvas = canvas->canvas;
    geometry.canvasMode = canvas->usedMode;
    geometry.intersectionReplaced = canvas->intersectionReplaced;
    DrizzleGeometryResolution resolution;
    resolution.geometry = std::move(geometry);
    return resolution;
}

} // namespace ImageIntegration
