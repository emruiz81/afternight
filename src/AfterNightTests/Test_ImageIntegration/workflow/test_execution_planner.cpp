// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Test_ImageIntegration/ImportSession.h"
#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/ExecutionPlanner.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowStageCompanions.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapIdentity.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Result.h>

namespace
{
/// The side of every frame these cases write, in pixels.
constexpr std::uint32_t kPlannerFrameSide = 100u;

/// The drizzled canvas every descriptor these cases write states.
constexpr std::uint32_t kDrizzleOutputSide = 200u;

FrameInfo makeFrame(FrameType type, const QString& filename)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, QSize(100, 100), 60.0, 0.0, 100, 10, QStringLiteral("L"), QStringLiteral("M42"),
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2025-01-01"), {});
}

void touchFile(const QString& path)
{
    const QFileInfo info(path);
    QDir dir(info.absolutePath());
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
}

/// Writes a grey light frame as a capture program does. With `sipA20`, the frame carries a tangent-plane solution whose
/// forward distortion's `A_2_0` coefficient is that value - the distortion a drizzle map inherits from the frame's own
/// header. Two frames that differ only in that coefficient are the same length on disk.
void writePlannerFrame(const QString& path, const std::optional<double> sipA20 = std::nullopt,
                       const std::uint32_t width = kPlannerFrameSide, const std::uint32_t height = kPlannerFrameSide)
{
    using an::tests::fixtures::FixtureCard;
    using an::tests::fixtures::FixtureCardKind;

    an::tests::fixtures::FixtureImage frame = an::tests::fixtures::acquisitionFrame(
        "planner-frame", an::tests::fixtures::AcquisitionFacts{.kind = "Light Frame"}, width, height);
    if (sipA20.has_value())
    {
        const auto text = [](std::string keyword, std::string value) {
            return FixtureCard{std::move(keyword), std::move(value), std::string(), FixtureCardKind::Text};
        };
        const auto real = [](std::string keyword, const double value) {
            return FixtureCard{std::move(keyword), an::tests::fixtures::formatDouble(value), std::string(),
                               FixtureCardKind::Real};
        };
        const auto integer = [](std::string keyword, const std::int64_t value) {
            return FixtureCard{std::move(keyword), std::to_string(value), std::string(), FixtureCardKind::Integer};
        };
        frame.cards.push_back(text("RADESYS", "ICRS"));
        frame.cards.push_back(text("CTYPE1", "RA---TAN-SIP"));
        frame.cards.push_back(text("CTYPE2", "DEC--TAN-SIP"));
        frame.cards.push_back(real("CRPIX1", static_cast<double>(width) / 2.0));
        frame.cards.push_back(real("CRPIX2", static_cast<double>(height) / 2.0));
        frame.cards.push_back(real("CRVAL1", 120.0));
        frame.cards.push_back(real("CRVAL2", 30.0));
        frame.cards.push_back(real("CD1_1", -0.0002));
        frame.cards.push_back(real("CD1_2", 0.0));
        frame.cards.push_back(real("CD2_1", 0.0));
        frame.cards.push_back(real("CD2_2", 0.0002));
        frame.cards.push_back(integer("A_ORDER", 2));
        frame.cards.push_back(real("A_2_0", *sipA20));
        frame.cards.push_back(integer("B_ORDER", 2));
        frame.cards.push_back(real("B_0_2", 0.0005));
    }

    std::string error;
    ASSERT_TRUE(an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
        << error;
}

bool writeTestJsonFile(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    const QFileInfo info(file.fileName());
    QDir dir(info.absolutePath());
    if ((!dir.exists() && !dir.mkpath(QStringLiteral("."))) || !file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size();
}

std::optional<QJsonObject> readTestJsonFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        return std::nullopt;
    }
    return document.object();
}

/// Writes a normalization artifact and the sidecar the normalization stage publishes beside it, stating the
/// coefficients' current family identity.
void writeNormalizationArtifact(const QString& artifactPath, const WorkflowManifest::IntegrationGroup& group,
                                const std::vector<FrameId>& effectiveFrames)
{
    touchFile(artifactPath);

    std::vector<FrameId> reference_frames = group.normalizationReferenceFrames;
    if (reference_frames.empty())
    {
        reference_frames = group.lightFiles;
        std::sort(reference_frames.begin(), reference_frames.end());
        reference_frames.erase(std::unique(reference_frames.begin(), reference_frames.end()), reference_frames.end());
        ASSERT_FALSE(reference_frames.empty());
        reference_frames = {reference_frames.front()};
    }

    QJsonObject meta;
    meta.insert(QStringLiteral("schemaVersion"), ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
    ImageIntegration::schema::insertArtifactFamilyIdentity(
        meta, ImageIntegration::schema::ArtifactFamily::NormalizationCoefficients);
    meta.insert(QStringLiteral("stageId"), QStringLiteral("LightsNormalization"));
    meta.insert(QStringLiteral("integrationGroupKey"), group.groupKey.toString());
    meta.insert(QStringLiteral("inputsFingerprint"), computeFrameFileStampFingerprint(effectiveFrames));
    meta.insert(QStringLiteral("settingsFingerprint"),
                computeNormalizationSettingsFingerprint(group, reference_frames));
    meta.insert(QStringLiteral("coefficientPayloadSha256"), ArtifactTracker::contentDigestForFile(artifactPath));

    ASSERT_TRUE(writeTestJsonFile(artifactPath + QStringLiteral(".meta.json"), meta));
}

QString registeredLightPathFor(const QString& output_dir, const WorkflowManifest::IntegrationGroup& group,
                               const QString& inputPath);

bool createCurrentRegistrationMetricsDb(const QString& path)
{
    static int connection_index = 0;
    const QString connection_name = QStringLiteral("execution_planner_reg_metrics_%1").arg(connection_index++);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
    db.setDatabaseName(path);
    if (!db.open())
    {
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name);
        return false;
    }

    bool ok = false;
    {
        QSqlQuery query(db);
        ok = query.exec(
                 QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion)) &&
             query.exec(stage_utils::registrationMetricsCreateSql()) &&
             query.exec(stage_utils::registrationGroupStateCreateSql()) &&
             query.exec(stage_utils::starDetectionCacheCreateSql()) &&
             query.exec(stage_utils::registrationMatchedSourcesCreateSql()) &&
             query.exec(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()) &&
             query.exec(stage_utils::registrationMatchedSourcesSourceIndexCreateSql());
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
    return ok;
}

/// Writes a registration checkpoint for every light of the manifest's registration groups: each row holds the identity
/// as the writer writes it, which inherits the frame's own distortion unless `distortionProvenance` states another.
bool createRegistrationMetricsDbForManifest(
    const QString& path, const WorkflowManifest& manifest, std::optional<int> versionOverride = std::nullopt,
    bool registrationSucceeded = true,
    std::optional<stage_utils::SourceDistortionProvenance> distortionProvenance = std::nullopt)
{
    if (!createCurrentRegistrationMetricsDb(path))
    {
        return false;
    }

    const an::Result<an::RegistrationTransform> identity = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Affine});
    if (!identity.hasValue())
    {
        return false;
    }
    const QString transform_json = stage_utils::serializeTransformJson(
        *identity, distortionProvenance.value_or(stage_utils::SourceDistortionProvenance::inheritMetadata()));

    static int connection_index = 0;
    const QString connection_name = QStringLiteral("execution_planner_reg_metric_rows_%1").arg(connection_index++);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
    db.setDatabaseName(path);
    if (!db.open())
    {
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name);
        return false;
    }

    bool ok = true;
    {
        QSqlQuery query(db);
        for (const auto& group : manifest.registrationGroups)
        {
            using ImageIntegration::RegistrationHashUtils::effectiveRegistrationHash;
            using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
            using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
            using ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod;
            using ImageIntegration::RegistrationSettingsUtils::usesStarDetection;

            const int version = versionOverride.value_or(group.settings.version);
            for (const auto& frame : group.lightFiles)
            {
                QString output_path;
                const auto integration_it =
                    std::find_if(manifest.integrationGroups.cbegin(), manifest.integrationGroups.cend(),
                                 [&](const WorkflowManifest::IntegrationGroup& integration) {
                                     return integration.registrationGroupKey == group.groupKey &&
                                            std::find(integration.lightFiles.cbegin(), integration.lightFiles.cend(),
                                                      frame) != integration.lightFiles.cend();
                                 });
                if (integration_it != manifest.integrationGroups.cend())
                {
                    output_path =
                        registeredLightPathFor(manifest.outputDirectory, *integration_it, frame.canonicalPath());
                }

                query.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_metrics ("
                                             "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                                             "succeeded, output_path, output_size_bytes, output_modified_at_ms, "
                                             "output_identity_token, transform_json, updated_at) "
                                             "VALUES (:frame_id, :group_key, :settings_version, :stars, :fwhm, :snr, "
                                             ":eccentricity, :succeeded, :output_path, :output_size_bytes, "
                                             ":output_modified_at_ms, :output_identity_token, :transform_json, "
                                             ":updated_at)"));
                query.bindValue(QStringLiteral(":frame_id"), frame.canonicalPath());
                query.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
                query.bindValue(QStringLiteral(":settings_version"), version);
                query.bindValue(QStringLiteral(":stars"), 100);
                query.bindValue(QStringLiteral(":fwhm"), 2.5);
                query.bindValue(QStringLiteral(":snr"), 50.0);
                query.bindValue(QStringLiteral(":eccentricity"), 0.1);
                query.bindValue(QStringLiteral(":succeeded"), registrationSucceeded ? 1 : 0);
                query.bindValue(QStringLiteral(":output_path"), output_path);
                const QFileInfo output_info(output_path);
                query.bindValue(QStringLiteral(":output_size_bytes"), output_info.isFile() ? output_info.size() : -1);
                query.bindValue(QStringLiteral(":output_modified_at_ms"),
                                output_info.isFile() ? output_info.lastModified().toMSecsSinceEpoch() : -1);
                query.bindValue(QStringLiteral(":output_identity_token"),
                                output_info.isFile()
                                    ? ArtifactTracker::fileIdentityForPath(output_info.absoluteFilePath())
                                    : QStringLiteral(""));
                query.bindValue(QStringLiteral(":transform_json"), transform_json);
                query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentSecsSinceEpoch());
                ok = query.exec() && ok;
            }

            query.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_group_state ("
                                         "group_key, settings_version, detection_hash, registration_hash, "
                                         "reference_frame_id, light_files_hash, calibration_hash, output_wcs_json, "
                                         "updated_at) "
                                         "VALUES (:group_key, :settings_version, :detection_hash, :registration_hash, "
                                         ":reference_frame_id, :light_files_hash, :calibration_hash, :output_wcs_json, "
                                         ":updated_at)"));
            query.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
            query.bindValue(QStringLiteral(":settings_version"), version);
            query.bindValue(QStringLiteral(":detection_hash"), registrationDetectionHash(group.settings));
            query.bindValue(
                QStringLiteral(":registration_hash"),
                effectiveRegistrationHash(group.settings, manifest.plateSolve, manifest.astrometryApiKeyHash));
            const bool auto_reference_runtime =
                group.settings.autoReferenceDuringRegistration &&
                usesStarDetection(normalizeRegistrationMethod(group.settings.registrationMethod));
            query.bindValue(QStringLiteral(":reference_frame_id"),
                            !auto_reference_runtime && group.referenceFrame.has_value()
                                ? group.referenceFrame->canonicalPath()
                                : QString());
            query.bindValue(QStringLiteral(":light_files_hash"), hashFrameIdList(group.lightFiles));
            query.bindValue(QStringLiteral(":calibration_hash"),
                            computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles));
            query.bindValue(QStringLiteral(":output_wcs_json"), QString());
            query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentSecsSinceEpoch());
            ok = query.exec() && ok;
        }
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
    return ok;
}

bool rewriteRegistrationMetricFrameIdsWithOtherCaseAndSeparators(const QString& path, const GroupKey& group_key,
                                                                 const std::vector<FrameId>& frames)
{
    static int connection_index = 0;
    const QString connection_name = QStringLiteral("execution_planner_alias_case_%1").arg(connection_index++);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
    db.setDatabaseName(path);
    if (!db.open())
    {
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name);
        return false;
    }

    bool ok = db.transaction();
    {
        QSqlQuery query(db);
        for (const auto& frame : frames)
        {
            QString alias_frame_id = frame.canonicalPath();
#ifdef Q_OS_WIN
            alias_frame_id = alias_frame_id.toUpper();
#endif
            alias_frame_id.replace(QLatin1Char('/'), QLatin1Char('\\'));
            query.prepare(QStringLiteral("UPDATE registration_metrics SET frame_id = :alias_frame_id "
                                         "WHERE group_key = :group_key AND frame_id = :frame_id"));
            query.bindValue(QStringLiteral(":alias_frame_id"), alias_frame_id);
            query.bindValue(QStringLiteral(":group_key"), group_key.toString());
            query.bindValue(QStringLiteral(":frame_id"), frame.canonicalPath());
            ok = ok && query.exec() && query.numRowsAffected() == 1;
        }
    }
    ok = ok && db.commit();
    if (!ok)
    {
        db.rollback();
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_name);
    return ok;
}

QString registeredLightPathFor(const QString& output_dir, const WorkflowManifest::IntegrationGroup& group,
                               const QString& inputPath)
{
    const QFileInfo inputInfo(inputPath);
    QString filename = inputInfo.completeBaseName() + QStringLiteral("_reg");
    const QString suffix = inputInfo.suffix();
    if (!suffix.isEmpty())
    {
        filename += QLatin1Char('.') + suffix;
    }

    const QString group_dir_name = ImageIntegration::sanitizeFilename(
        group.registrationGroupKey.toString().trimmed().isEmpty() ? group.groupKey.toString()
                                                                  : group.registrationGroupKey.toString());
    const QDir output_root(output_dir);
    const QDir registered_root(output_root.filePath(QStringLiteral("registered_lights")));
    const QDir group_dir(registered_root.filePath(group_dir_name));
    return group_dir.filePath(filename);
}

/// What a registration checkpoint holds for each light, keyed by the frame identity it was written under.
struct RegistrationRecords
{
    std::unordered_map<QString, an::RegistrationTransform> transforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
};

/// Reads a group's checkpoint rows back as the transform records they hold, read as any reader of the record reads
/// them.
bool readRegistrationRecords(const QString& path, const GroupKey& groupKey, const int settingsVersion,
                             RegistrationRecords& records, QString& error)
{
    static int connection_index = 0;
    const QString connection_name = QStringLiteral("execution_planner_reg_records_%1").arg(connection_index++);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT frame_id, transform_json FROM registration_metrics "
                                         "WHERE group_key = :group_key AND settings_version = :settings_version"));
            query.bindValue(QStringLiteral(":group_key"), groupKey.toString());
            query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
            ok = query.exec();
            while (ok && query.next())
            {
                an::RegistrationTransform transform;
                stage_utils::SourceDistortionProvenance provenance;
                if (!stage_utils::parseRegistrationTransformJson(query.value(1).toString(), transform, &provenance,
                                                                 error))
                {
                    ok = false;
                    break;
                }
                const QString frame_id = query.value(0).toString();
                records.transforms.emplace(frame_id, transform);
                records.provenance.emplace(frame_id, provenance);
            }
            query.finish();
            db.close();
        }
        else
        {
            error = QStringLiteral("The registration checkpoint could not be opened.");
        }
    }
    QSqlDatabase::removeDatabase(connection_name);
    return ok;
}

/// A drizzle group's persisted maps as the drizzle stage publishes them: one descriptor document per light and the
/// group's sidecar listing them, with the fingerprints the sidecar states.
struct DrizzleArtifacts
{
    QJsonObject sidecar;
    QString directory;
    std::vector<ImageIntegration::DrizzleDescriptorDocument> descriptors;
    QStringList descriptorFiles;
    std::unordered_map<QString, an::RegistrationTransform> transforms;
    std::unordered_map<QString, stage_utils::SourceDistortionProvenance> provenance;
    std::unordered_map<QString, std::optional<an::SipDistortion>> resolved;

    [[nodiscard]] bool valid() const
    {
        return !sidecar.isEmpty();
    }

    [[nodiscard]] QString transformsFingerprint() const
    {
        return stage_utils::fingerprintTransforms(transforms);
    }

    [[nodiscard]] QString sourceDistortionFingerprint() const
    {
        return stage_utils::fingerprintSourceDistortions(transforms, provenance, resolved);
    }

    [[nodiscard]] QString sidecarPath() const
    {
        return QDir(directory).filePath(QStringLiteral(".meta.json"));
    }

    [[nodiscard]] QString descriptorPath(const qsizetype index) const
    {
        return QDir(directory).filePath(descriptorFiles.at(index));
    }
};

/// An engine identity of the length the engine's record has; its content is not read by the planner.
QByteArray testEngineIdentity()
{
    return QByteArray(static_cast<qsizetype>(sizeof(an::DrizzleMapIdentity::bytes)), '\x5a');
}

bool publishDescriptor(const QString& path, const ImageIntegration::DrizzleDescriptorDocument& document)
{
    return ImageIntegration::publishDocument(path, ImageIntegration::drizzleDescriptorEnvelope(document)).ok();
}

/// Writes a drizzle group's descriptors and sidecar from the registration checkpoint the case wrote and each light's
/// header as `headerReader` reads it. An invalid result has already reported its failure.
DrizzleArtifacts writeDrizzleDescriptorArtifacts(const WorkflowManifest& manifest,
                                                 const WorkflowManifest::IntegrationGroup& group,
                                                 const ImageIntegration::FrameHeaderReader& headerReader)
{
    if (!group.drizzle.has_value() || !group.drizzle->enabled || !group.drizzlePixmapArtifactDir.has_value() ||
        !headerReader)
    {
        ADD_FAILURE() << "A drizzle artifact needs an enabled drizzle group, its directory and a header reader.";
        return {};
    }
    const auto registration = std::find_if(manifest.registrationGroups.cbegin(), manifest.registrationGroups.cend(),
                                           [&](const WorkflowManifest::RegistrationGroup& candidate) {
                                               return candidate.groupKey == group.registrationGroupKey;
                                           });
    if (registration == manifest.registrationGroups.cend())
    {
        ADD_FAILURE() << "The drizzle group's registration group is not in the manifest.";
        return {};
    }

    RegistrationRecords records;
    QString error;
    if (!readRegistrationRecords(QDir(manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db")),
                                 registration->groupKey, registration->settings.version, records, error))
    {
        ADD_FAILURE() << "The registration checkpoint could not be read: " << error.toStdString();
        return {};
    }

    DrizzleArtifacts artifacts;
    artifacts.directory = group.drizzlePixmapArtifactDir.value();
    if (!QDir().mkpath(artifacts.directory))
    {
        ADD_FAILURE() << "The drizzle directory could not be created.";
        return {};
    }

    QJsonArray frames;
    int index = 0;
    for (const auto& frame : group.lightFiles)
    {
        const QString key = frame.canonicalPath();
        const auto transform = records.transforms.find(key);
        const auto provenance = records.provenance.find(key);
        const std::optional<ImageIntegration::FrameHeaderFacts> header = headerReader(key);
        if (transform == records.transforms.end() || provenance == records.provenance.end() || !header.has_value())
        {
            ADD_FAILURE() << "No checkpoint row or no readable header for " << key.toStdString();
            return {};
        }

        ImageIntegration::DrizzleDescriptorDocument document;
        document.frameId = key;
        document.sourceWidth = header->width;
        document.sourceHeight = header->height;
        document.outputWidth = kDrizzleOutputSide;
        document.outputHeight = kDrizzleOutputSide;
        document.scale = static_cast<double>(group.drizzle->scale);
        document.transform = transform->second;
        document.sourceDistortion = provenance->second;
        document.resolvedSourceDistortion =
            provenance->second.policy == stage_utils::SourceDistortionPolicy::InheritMetadata
                ? header->distortion
                : provenance->second.model;
        document.engineIdentity = testEngineIdentity();

        const QString descriptor_file = QStringLiteral("frame_%1.descriptor").arg(index++);
        if (!publishDescriptor(QDir(artifacts.directory).filePath(descriptor_file), document))
        {
            ADD_FAILURE() << "The descriptor for " << key.toStdString() << " could not be published.";
            return {};
        }

        QJsonObject entry;
        entry.insert(QStringLiteral("frameId"), frame.toJson());
        entry.insert(QStringLiteral("descriptorFile"), descriptor_file);
        frames.append(entry);

        artifacts.transforms.emplace(key, document.transform);
        artifacts.provenance.emplace(key, document.sourceDistortion);
        artifacts.resolved.emplace(key, document.resolvedSourceDistortion);
        artifacts.descriptors.push_back(document);
        artifacts.descriptorFiles.push_back(descriptor_file);
    }

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schemaVersion"), ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion);
    ImageIntegration::schema::insertArtifactFamilyIdentity(sidecar,
                                                           ImageIntegration::schema::ArtifactFamily::DrizzleDescriptor);
    sidecar.insert(QStringLiteral("stageId"), QStringLiteral("LightsDrizzle"));
    sidecar.insert(QStringLiteral("integrationGroupKey"), group.groupKey.toString());
    sidecar.insert(QStringLiteral("outputWidth"), static_cast<int>(kDrizzleOutputSide));
    sidecar.insert(QStringLiteral("outputHeight"), static_cast<int>(kDrizzleOutputSide));
    sidecar.insert(QStringLiteral("scale"), static_cast<double>(group.drizzle->scale));
    sidecar.insert(QStringLiteral("pixelFraction"), static_cast<double>(group.drizzle->pixelFraction));
    sidecar.insert(QStringLiteral("kernel"), group.drizzle->kernel);
    sidecar.insert(QStringLiteral("cfaAware"), group.drizzle->cfaAware);
    sidecar.insert(QStringLiteral("cfaPattern"), group.drizzle->cfaPattern);
    sidecar.insert(QStringLiteral("mapPersistencePolicy"), QStringLiteral("CompactDescriptorPreferred"));
    sidecar.insert(QStringLiteral("transformsFingerprint"), artifacts.transformsFingerprint());
    sidecar.insert(QStringLiteral("sourceDistortionFingerprint"), artifacts.sourceDistortionFingerprint());
    sidecar.insert(QStringLiteral("sourceInputsFingerprint"), computeFrameFileStampFingerprint(group.lightFiles));
    sidecar.insert(QStringLiteral("frames"), frames);
    if (!writeTestJsonFile(artifacts.sidecarPath(), sidecar))
    {
        ADD_FAILURE() << "The drizzle sidecar could not be written.";
        return {};
    }
    artifacts.sidecar = sidecar;
    return artifacts;
}

/// The plan's decisions read through a snapshot that reads each drizzle source's header, as the product prepares a run.
std::vector<ExecutionPlanDecision> decisionsReadingHeaders(const WorkflowManifest& manifest, const ExecutionPlan& plan,
                                                           const ImageIntegration::FrameHeaderReader& headerReader)
{
    const auto snapshot = ExecutionPreparationSnapshot::build(manifest, manifest, {}, headerReader);
    return buildExecutionPlanDecisions(manifest, plan, *snapshot);
}

/// The action of the first decision for `stage`; a missing decision is a failure, answered as a skip.
ExecutionPlanDecision::Action actionOf(const std::vector<ExecutionPlanDecision>& decisions, const QString& stage)
{
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(),
                                 [&](const ExecutionPlanDecision& decision) { return decision.stage == stage; });
    EXPECT_NE(it, decisions.cend()) << "No decision for " << stage.toStdString();
    return it != decisions.cend() ? it->action : ExecutionPlanDecision::Action::Skip;
}
} // namespace

class ExecutionPlannerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tempDir.isValid()) << "Failed to create temporary directory";
    }

    QString tempPath() const
    {
        return m_tempDir.path();
    }

    /// Opens the case's engine session, which the header reader reads through.
    void openSession()
    {
        an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(1u);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        m_session = std::make_unique<an::consumer::EngineSession>(std::move(*opened));
    }

    /// A header reader over the case's session; nothing before `openSession`.
    ImageIntegration::FrameHeaderReader headerReader() const
    {
        return m_session ? ImageIntegration::sessionFrameHeaderReader(*m_session)
                         : ImageIntegration::FrameHeaderReader{};
    }

    QTemporaryDir m_tempDir;
    std::unique_ptr<an::consumer::EngineSession> m_session;
};

TEST_F(ExecutionPlannerTest, FullPlanWhenNoHistory)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ExecutionPlanner planner(model);
    auto plan = planner.buildPlan(manifest, std::nullopt);

    EXPECT_EQ(plan.mode, ExecutionMode::Full);
}

TEST_F(ExecutionPlannerTest, IncrementalPlanOnStackingSettingsChange)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest previous = builder.build();

    // Change stacking settings
    const GroupKey groupKey = model.integrationGroups().front();
    StackingSettings settings;
    settings.sigmaHigh = 5.0;
    model.setStackingSettings(FrameType::Light, groupKey, settings);

    WorkflowManifest current = builder.build();

    ExecutionPlanner planner(model);
    auto plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_FALSE(plan.integrationGroupsToProcess.empty());
    EXPECT_EQ(plan.integrationGroupsToProcess.front().key, groupKey);
}

TEST_F(ExecutionPlannerTest, ReusePolicyAfterForcedRunDoesNotDirtyCalibrationOrRegistration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());

    WorkflowManifest previous;
    previous.outputDirectory = tempPath();
    previous.reprocessExisting = true;
    previous.calibrateLightsInMemory = true;

    WorkflowManifest::CalibrationStage flat_stage;
    flat_stage.frameType = QStringLiteral("FLAT");

    WorkflowManifest::CalibrationStage::Group flat_group;
    flat_group.groupKey = GroupKey{QStringLiteral("flat|100x100|L|2025-01-01")};
    flat_group.outputFilename = QStringLiteral("master_flat.fit");
    flat_group.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("flat_a.fit"))),
                             FrameId(QDir(tempPath()).filePath(QStringLiteral("flat_b.fit")))};
    flat_group.skipIfExists = false;
    flat_stage.groups.push_back(flat_group);
    previous.flatStage = flat_stage;

    const FrameId light_a(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")));
    const FrameId light_b(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")));
    const FrameId flat_master(QDir(tempPath()).filePath(flat_group.outputFilename));

    for (const FrameId& light : {light_a, light_b})
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        params.flatPath = flat_master;
        previous.lightsCalibration.push_back(params);
    }

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.lightFiles = {light_a, light_b};
    previous.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    integration.outputFilename = QStringLiteral("stacked_l.fit");
    integration.registrationGroupKey = registration.groupKey;
    integration.lightFiles = {light_a, light_b};
    integration.stacking.weighting = QStringLiteral("PSF Signal Weight");
    integration.skipIfExists = false;
    previous.integrationGroups.push_back(integration);

    WorkflowManifest current = previous;
    current.reprocessExisting = false;
    current.flatStage->groups.front().skipIfExists = true;
    current.integrationGroups.front().skipIfExists = true;
    current.integrationGroups.front().stacking.weighting = QStringLiteral("None");

    touchFile(flat_master.canonicalPath());
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.biasGroupsToProcess.empty());
    EXPECT_TRUE(plan.darkGroupsToProcess.empty());
    EXPECT_TRUE(plan.flatGroupsToProcess.empty());
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), current.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, EmptyPlanWhenNoChanges)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Create the expected workflow artifacts so "no changes" is truly a no-op: the final stacks, and the registered
    // lights the registration database records, which the alignment stage reads as current.
    for (const auto& group : manifest.integrationGroups)
    {
        touchFile(QDir(tempPath()).filePath(group.outputFilename));

        const bool drizzle_enabled = group.drizzle.has_value() && group.drizzle->enabled;
        if (drizzle_enabled)
        {
            continue;
        }

        for (const auto& frame : group.lightFiles)
        {
            const QString inputPath = frame.canonicalPath();
            if (!inputPath.isEmpty())
            {
                touchFile(registeredLightPathFor(tempPath(), group, inputPath));
            }
        }
    }

    if (!manifest.registrationGroups.empty())
    {
        ASSERT_TRUE(createRegistrationMetricsDbForManifest(
            QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));
    }

    ExecutionPlanner planner(model);
    auto plan = planner.buildPlan(manifest, manifest);

    EXPECT_TRUE(plan.isEmpty());
}

TEST_F(ExecutionPlannerTest, RequestedIntegrationGroupCanBeForcedIntoIncrementalPlan)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    for (const auto& group : manifest.integrationGroups)
    {
        touchFile(QDir(tempPath()).filePath(group.outputFilename));

        for (const auto& frame : group.lightFiles)
        {
            const QString inputPath = frame.canonicalPath();
            if (!inputPath.isEmpty())
            {
                touchFile(registeredLightPathFor(tempPath(), group, inputPath));
            }
        }
    }

    if (!manifest.registrationGroups.empty())
    {
        ASSERT_TRUE(createRegistrationMetricsDbForManifest(
            QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));
    }

    ExecutionPlanner planner(model);
    ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    ASSERT_TRUE(plan.isEmpty());
    ASSERT_FALSE(manifest.integrationGroups.empty());

    plan.ensureIntegrationGroupsToProcess({manifest.integrationGroups.front().groupKey});

    EXPECT_FALSE(plan.isEmpty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), manifest.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, StaleRegistrationSchemaTriggersIncrementalPlanEvenWhenOutputsExist)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    const QString db_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    ASSERT_TRUE(createCurrentRegistrationMetricsDb(db_path));

    {
        static int connection_index = 0;
        const QString connection_name = QStringLiteral("execution_planner_stale_schema_%1").arg(connection_index++);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
        db.setDatabaseName(db_path);
        ASSERT_TRUE(db.open());

        {
            QSqlQuery query(db);
            ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA user_version = 2")));
        }

        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name);
    }

    for (const auto& group : manifest.integrationGroups)
    {
        touchFile(QDir(tempPath()).filePath(group.outputFilename));

        const bool drizzle_enabled = group.drizzle.has_value() && group.drizzle->enabled;
        if (drizzle_enabled)
        {
            continue;
        }

        for (const auto& frame : group.lightFiles)
        {
            const QString inputPath = frame.canonicalPath();
            if (!inputPath.isEmpty())
            {
                touchFile(registeredLightPathFor(tempPath(), group, inputPath));
            }
        }
    }

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_FALSE(plan.isEmpty());
    EXPECT_FALSE(plan.registrationGroupsToProcess.empty());
    EXPECT_FALSE(plan.integrationGroupsToProcess.empty());
}

TEST_F(ExecutionPlannerTest, UnreadableRegistrationCacheTriggersIncrementalPlanEvenWhenOutputsExist)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    const QString db_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    ASSERT_TRUE(QDir().mkpath(db_path));

    for (const auto& group : manifest.integrationGroups)
    {
        touchFile(QDir(tempPath()).filePath(group.outputFilename));

        const bool drizzle_enabled = group.drizzle.has_value() && group.drizzle->enabled;
        if (drizzle_enabled)
        {
            continue;
        }

        for (const auto& frame : group.lightFiles)
        {
            const QString inputPath = frame.canonicalPath();
            if (!inputPath.isEmpty())
            {
                touchFile(registeredLightPathFor(tempPath(), group, inputPath));
            }
        }
    }

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_FALSE(plan.isEmpty());
    EXPECT_FALSE(plan.registrationGroupsToProcess.empty());
    EXPECT_FALSE(plan.integrationGroupsToProcess.empty());
}

TEST_F(ExecutionPlannerTest, MissingArtifactsTriggersIncrementalPlanEvenWhenNoChanges)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ExecutionPlanner planner(model);
    auto plan = planner.buildPlan(manifest, manifest);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_FALSE(plan.isEmpty()) << "Planner should schedule work when outputs are missing.";
}

TEST_F(ExecutionPlannerTest, ExistingRegistrationMetricsPreventRegistrationRecomputeWithoutFullHistory)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, std::nullopt);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), manifest.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, PreviousSettingsVersionMetricsForceFullPlanWithoutHistory)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    ASSERT_FALSE(manifest.registrationGroups.empty());

    manifest.registrationGroups.front().settings.version = 2;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest, 1));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, std::nullopt);

    EXPECT_EQ(plan.mode, ExecutionMode::Full);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    EXPECT_TRUE(plan.integrationGroupsToProcess.empty());
}

TEST_F(ExecutionPlannerTest, PreviousSettingsVersionMetricsDirtyRegistrationWithHistory)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    ASSERT_FALSE(manifest.registrationGroups.empty());

    manifest.registrationGroups.front().settings.version = 2;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest, 1));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    ASSERT_FALSE(plan.registrationGroupsToProcess.empty());
    EXPECT_EQ(plan.registrationGroupsToProcess.front(), manifest.registrationGroups.front().groupKey);
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), manifest.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, MissingRegisteredOutputsDoNotDirtyRegistrationWhenMetricsExist)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));
    ASSERT_FALSE(manifest.integrationGroups.empty());
    touchFile(QDir(tempPath()).filePath(manifest.integrationGroups.front().outputFilename));

    // No registered light was written, so the alignment stage finds its outputs missing.
    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), manifest.integrationGroups.front().groupKey);

    // The group is selected for its missing registered lights, which the alignment stage writes again.
    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const QString compactGroup = manifest.integrationGroups.front().groupKey.toCompactString();
    const auto alignment = std::find_if(decisions.cbegin(), decisions.cend(), [&](const ExecutionPlanDecision& d) {
        return d.stage == QStringLiteral("Lights Alignment") && d.groupKey == compactGroup;
    });
    ASSERT_NE(alignment, decisions.cend());
    EXPECT_EQ(alignment->action, ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, MissingDiscardableRegisteredOutputsRemainNoOpWhenFinalStackIsCurrent)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("discardable_light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    ASSERT_FALSE(manifest.integrationGroups.empty());
    ASSERT_FALSE(manifest.registrationGroups.empty());
    manifest.registrationGroups.front().settings.alignmentMode = QStringLiteral("standard");
    manifest.registrationGroups.front().settings.interpolation = QStringLiteral("Bilinear");
    auto& group = manifest.integrationGroups.front();
    group.stacking.stackMethod = QStringLiteral("Average");
    group.stacking.rejectionMethod = QStringLiteral("None");
    group.stacking.normalizationMode = NormalizationMode::None;
    group.stacking.weighting = QStringLiteral("None");
    group.stacking.solveFinalMasterIfNeeded = false;
    group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));
    touchFile(QDir(tempPath()).filePath(group.outputFilename));

    // The registered lights are missing, and a group whose registered images are discardable has its lights warped as
    // the stack reads them.
    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    EXPECT_TRUE(plan.isEmpty());

    // The same missing outputs are needed when the group keeps its registered images, which the stack then reads.
    group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Retained;
    const ExecutionPlan materialized_plan = planner.buildPlan(manifest, manifest);
    ASSERT_EQ(materialized_plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(materialized_plan.integrationGroupsToProcess.front(), group.groupKey);
}

TEST_F(ExecutionPlannerTest, CompletedRegistrationCheckpointOverridesStaleSuccessfulManifest)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    const FrameInfo light = makeFrame(FrameType::Light, QStringLiteral("light.fit"));
    touchFile(light.id.canonicalPath());
    model.addFrames(FrameType::Light, {light});

    ManifestBuilder builder(model);
    WorkflowManifest current = builder.build();
    WorkflowManifest previous = current;
    ASSERT_FALSE(current.registrationGroups.empty());

    previous.registrationGroups.front().settings.threshold += 0.5;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), current.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, StaleDrizzleStackIsReplannedWhenRegistrationCheckpointIsReusable)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    const FrameInfo light_a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"));
    const FrameInfo light_b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"));
    touchFile(light_a.id.canonicalPath());
    touchFile(light_b.id.canonicalPath());
    model.addFrames(FrameType::Light, {light_a, light_b});

    ManifestBuilder builder(model);
    WorkflowManifest current = builder.build();
    ASSERT_FALSE(current.registrationGroups.empty());
    ASSERT_FALSE(current.integrationGroups.empty());
    current.integrationGroups.front().skipIfExists = true;
    current.integrationGroups.front().drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    current.integrationGroups.front().drizzle->enabled = true;

    WorkflowManifest previous = current;
    previous.registrationGroups.front().settings.threshold += 0.5;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    const QString stacked_path = QDir(tempPath()).filePath(current.integrationGroups.front().outputFilename);
    touchFile(stacked_path);
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(stacked_path, QStringLiteral("obsolete-registration-provenance"));
    }

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), current.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, ReplacedGeneratedMasterWithSameMetadataIsReplanned)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|100x100")};
    group.outputFilename = QStringLiteral("master_bias.fit");
    group.skipIfExists = true;
    group.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("bias_a.fit")))};
    touchFile(group.inputFiles.front().canonicalPath());
    stage.groups.push_back(group);
    manifest.biasStage = stage;

    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    touchFile(output_path);
    const QDateTime recorded_modified = QFileInfo(output_path).lastModified();
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(output_path, computeCalibrationGroupHash(manifest, group));
        ASSERT_TRUE(tracker.isArtifactValid(output_path, computeCalibrationGroupHash(manifest, group)));
    }

    QFile replacement(output_path);
    ASSERT_TRUE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(replacement.write("y"), 1);
    replacement.close();
    ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacement.setFileTime(recorded_modified, QFileDevice::FileModificationTime));
    replacement.close();

    const QFileInfo replacement_info(output_path);
    ASSERT_EQ(replacement_info.size(), 1);
    ASSERT_EQ(replacement_info.lastModified().toMSecsSinceEpoch(), recorded_modified.toMSecsSinceEpoch());

    ExecutionPlanner planner;
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);

    ASSERT_EQ(plan.biasGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.biasGroupsToProcess.front(), group.groupKey);
}

TEST_F(ExecutionPlannerTest, CompletedRegistrationCheckpointInvalidatesWhenLightSampleChanges)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    const FrameInfo light = makeFrame(FrameType::Light, QStringLiteral("light.fit"));
    touchFile(light.id.canonicalPath());
    model.addFrames(FrameType::Light, {light});

    ManifestBuilder builder(model);
    WorkflowManifest current = builder.build();
    WorkflowManifest previous = current;
    ASSERT_FALSE(current.registrationGroups.empty());

    previous.registrationGroups.front().settings.threshold += 0.5;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    QFile changed_light(light.id.canonicalPath());
    ASSERT_TRUE(changed_light.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(changed_light.write("changed"), 7);
    changed_light.close();

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    ASSERT_EQ(plan.registrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.registrationGroupsToProcess.front(), current.registrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, FailedRegistrationMetricPreventsCheckpointReuse)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    const FrameInfo light = makeFrame(FrameType::Light, QStringLiteral("light.fit"));
    touchFile(light.id.canonicalPath());
    model.addFrames(FrameType::Light, {light});

    ManifestBuilder builder(model);
    WorkflowManifest current = builder.build();
    WorkflowManifest previous = current;
    ASSERT_FALSE(current.registrationGroups.empty());

    previous.registrationGroups.front().settings.threshold += 0.5;
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current, std::nullopt, false));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    ASSERT_EQ(plan.registrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.registrationGroupsToProcess.front(), current.registrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, ReprocessExistingForcesFullExecutionMode)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest previous = builder.build();

    builder.setReprocessExisting(true);
    WorkflowManifest current = builder.build();
    ASSERT_TRUE(current.reprocessExisting);

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Full);
    EXPECT_TRUE(plan.biasGroupsToProcess.empty());
    EXPECT_TRUE(plan.darkGroupsToProcess.empty());
    EXPECT_TRUE(plan.flatGroupsToProcess.empty());
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    EXPECT_TRUE(plan.integrationGroupsToProcess.empty());
}

TEST_F(ExecutionPlannerTest, LocalNormalizationChangeMarksOnlyAffectedIntegrationGroupDirty)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());

    WorkflowManifest previous;
    previous.outputDirectory = tempPath();
    previous.calibrateLightsInMemory = true;

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|shared")};
    registration.settings.version = 1;

    for (int i = 0; i < 10; ++i)
    {
        const QString path = QDir(tempPath()).filePath(QStringLiteral("light_%1.fit").arg(i));
        const FrameId frame(path);
        registration.lightFiles.push_back(frame);

        WorkflowManifest::LightsCalibrationParams cal;
        cal.lightPath = frame;
        previous.lightsCalibration.push_back(cal);
    }
    previous.registrationGroups.push_back(registration);

    for (int group_index = 0; group_index < 5; ++group_index)
    {
        WorkflowManifest::IntegrationGroup integration;
        integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|%1").arg(group_index)}};
        integration.outputFilename = QStringLiteral("stack_%1.fit").arg(group_index);
        integration.registrationGroupKey = registration.groupKey;
        integration.stacking.normalizationMode = NormalizationMode::Local;
        integration.stacking.localNormalization.enabled = true;
        integration.stacking.localNormalization.gridSize = 48;
        integration.lightFiles.push_back(registration.lightFiles[2 * group_index]);
        integration.lightFiles.push_back(registration.lightFiles[2 * group_index + 1]);
        previous.integrationGroups.push_back(integration);
    }

    WorkflowManifest current = previous;
    ASSERT_EQ(current.integrationGroups.size(), 5u);
    current.integrationGroups[2].stacking.localNormalization.gridSize = 96;

    for (const auto& group : current.integrationGroups)
    {
        touchFile(QDir(tempPath()).filePath(group.outputFilename));
        for (const auto& frame : group.lightFiles)
        {
            touchFile(registeredLightPathFor(tempPath(), group, frame.canonicalPath()));
        }
    }
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.biasGroupsToProcess.empty());
    EXPECT_TRUE(plan.darkGroupsToProcess.empty());
    EXPECT_TRUE(plan.flatGroupsToProcess.empty());
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), current.integrationGroups[2].groupKey);
}

TEST_F(ExecutionPlannerTest, PlateSolveSettingChangeMarksRegistrationAndIntegrationDirty)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest previous = builder.build();
    WorkflowManifest current = previous;
    current.plateSolve.defaultSolver = QStringLiteral("astrometry.net");

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);

    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    ASSERT_EQ(plan.registrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.registrationGroupsToProcess.front(), current.registrationGroups.front().groupKey);
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), current.integrationGroups.front().groupKey);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsDescribeIncrementalReuseAndRecompute)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("decision_light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest previous = builder.build();

    const GroupKey groupKey = model.integrationGroups().front();
    StackingSettings settings;
    settings.sigmaHigh = 4.75;
    model.setStackingSettings(FrameType::Light, groupKey, settings);
    WorkflowManifest current = builder.build();

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), current));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(current, previous);
    const auto decisions = buildExecutionPlanDecisions(current, plan, previous);

    auto findDecision = [&](const QString& stage, const QString& group) -> const ExecutionPlanDecision* {
        for (const auto& decision : decisions)
        {
            if (decision.stage == stage && decision.groupKey == group)
            {
                return &decision;
            }
        }
        return nullptr;
    };

    const QString compactGroup = current.integrationGroups.front().groupKey.toCompactString();
    const auto* stacking = findDecision(QStringLiteral("Lights Stacking"), compactGroup);
    ASSERT_NE(stacking, nullptr);
    EXPECT_EQ(stacking->action, ExecutionPlanDecision::Action::Recompute);
    EXPECT_TRUE(stacking->reason.contains(QStringLiteral("Stacking"), Qt::CaseInsensitive));
    EXPECT_GT(stacking->estimatedSeconds, 0);
    EXPECT_TRUE(stacking->outputs.contains(current.integrationGroups.front().outputFilename));

    const auto* registration = findDecision(QStringLiteral("Lights Registration"),
                                            current.registrationGroups.front().groupKey.toCompactString());
    ASSERT_NE(registration, nullptr);
    EXPECT_EQ(registration->action, ExecutionPlanDecision::Action::Reuse);
    EXPECT_TRUE(registration->reason.contains(QStringLiteral("up to date"), Qt::CaseInsensitive));
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReuseAlignmentWhenOnlyStackOutputIsMissing)
{
    IntegrationDataModel model;
    model.setOutputDirectory(tempPath());
    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("decision_light.fit"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_FALSE(manifest.integrationGroups.empty());
    ASSERT_FALSE(manifest.registrationGroups.empty());

    // The registered lights are on disk and recorded in the registration database, so the alignment stage finds them
    // current.
    for (const auto& group : manifest.integrationGroups)
    {
        for (const auto& frame : group.lightFiles)
        {
            const QString inputPath = frame.canonicalPath();
            if (!inputPath.isEmpty())
            {
                touchFile(registeredLightPathFor(tempPath(), group, inputPath));
            }
        }
    }
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));

    ExecutionPlanner planner(model);
    const ExecutionPlan plan = planner.buildPlan(manifest, manifest);
    ASSERT_EQ(plan.mode, ExecutionMode::Incremental);
    ASSERT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_FALSE(plan.integrationGroupsToProcess.empty());

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const QString compactGroup = manifest.integrationGroups.front().groupKey.toCompactString();

    const auto alignmentIt =
        std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Alignment") && decision.groupKey == compactGroup;
        });

    ASSERT_NE(alignmentIt, decisions.end());
    EXPECT_EQ(alignmentIt->action, ExecutionPlanDecision::Action::Reuse);
    EXPECT_TRUE(alignmentIt->reason.contains(QStringLiteral("current"), Qt::CaseInsensitive));
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReuseExistingNormalizationArtifactForOutputOnlyRerun)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsAlignment"), QStringLiteral("LightsNormalization"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = GroupKey{};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.normalizationCoefficientsArtifactPath =
        QDir(tempPath())
            .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/int_L/"
                                     "coefficients.json"));
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("light_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")))};
    manifest.integrationGroups.push_back(group);

    for (const auto& frame : group.lightFiles)
        touchFile(frame.canonicalPath());
    writeNormalizationArtifact(group.normalizationCoefficientsArtifactPath.value(), group, group.lightFiles);

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto normalizationIt =
        std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Normalization") &&
                   decision.groupKey == group.groupKey.toCompactString();
        });

    ASSERT_NE(normalizationIt, decisions.end());
    EXPECT_EQ(normalizationIt->action, ExecutionPlanDecision::Action::Reuse);
    EXPECT_TRUE(normalizationIt->reason.contains(QStringLiteral("reused"), Qt::CaseInsensitive));
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsRecomputeNormalizationWhenAlignmentRecomputes)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsAlignment"), QStringLiteral("LightsNormalization"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.normalizationCoefficientsArtifactPath =
        QDir(tempPath())
            .filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/int_L/"
                                     "coefficients.json"));
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("light_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")))};
    manifest.integrationGroups.push_back(group);

    writeNormalizationArtifact(group.normalizationCoefficientsArtifactPath.value(), group, group.lightFiles);

    // No registered light was written: the alignment stage finds its outputs missing, so it recomputes them.

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto alignmentIt =
        std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Alignment") &&
                   decision.groupKey == group.groupKey.toCompactString();
        });
    const auto normalizationIt =
        std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Normalization") &&
                   decision.groupKey == group.groupKey.toCompactString();
        });

    ASSERT_NE(alignmentIt, decisions.end());
    EXPECT_EQ(alignmentIt->action, ExecutionPlanDecision::Action::Recompute);
    ASSERT_NE(normalizationIt, decisions.end());
    EXPECT_EQ(normalizationIt->action, ExecutionPlanDecision::Action::Recompute);
    EXPECT_TRUE(normalizationIt->reason.contains(QStringLiteral("Alignment"), Qt::CaseInsensitive));
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReuseGlobalNormalizationAndInvalidateChangedInputs)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsNormalization"), QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.normalizationCoefficientsArtifactPath =
        QDir(tempPath()).filePath(QStringLiteral("normalization/global_coefficients.json"));
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("global_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("global_b.fit")))};
    for (const auto& frame : group.lightFiles)
        touchFile(frame.canonicalPath());
    manifest.integrationGroups.push_back(group);

    writeNormalizationArtifact(group.normalizationCoefficientsArtifactPath.value(), group, group.lightFiles);

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    auto actionForNormalization = [&](const WorkflowManifest& candidate) {
        const auto decisions = buildExecutionPlanDecisions(candidate, plan, candidate);
        return actionOf(decisions, QStringLiteral("Lights Normalization"));
    };

    EXPECT_EQ(actionForNormalization(manifest), ExecutionPlanDecision::Action::Reuse);

    // A sidecar of the schema before this one states its coefficient schema and no family identity; it is never
    // reused.
    const QString meta_path = group.normalizationCoefficientsArtifactPath.value() + QStringLiteral(".meta.json");
    const std::optional<QJsonObject> current_meta = readTestJsonFile(meta_path);
    ASSERT_TRUE(current_meta.has_value());
    QJsonObject unfamilied_meta = *current_meta;
    unfamilied_meta.remove(QStringLiteral("artifactFamily"));
    unfamilied_meta.insert(QStringLiteral("schemaVersion"),
                           ImageIntegration::schema::kNormalizationArtifactSchemaVersion - 1);
    unfamilied_meta.insert(QStringLiteral("coefficientSchemaVersion"), 1);
    ASSERT_TRUE(writeTestJsonFile(meta_path, unfamilied_meta));
    EXPECT_EQ(actionForNormalization(manifest), ExecutionPlanDecision::Action::Recompute);
    ASSERT_TRUE(writeTestJsonFile(meta_path, *current_meta));
    EXPECT_EQ(actionForNormalization(manifest), ExecutionPlanDecision::Action::Reuse);

    QFile changed_input(group.lightFiles.front().canonicalPath());
    ASSERT_TRUE(changed_input.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(changed_input.write("changed"), 7);
    changed_input.close();

    EXPECT_EQ(actionForNormalization(manifest), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsInvalidateLocalNormalizationWhenSettingsChange)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsNormalization"), QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.normalizationCoefficientsArtifactPath =
        QDir(tempPath()).filePath(QStringLiteral("normalization/local_coefficients.json"));
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("local_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("local_b.fit")))};
    for (const auto& frame : group.lightFiles)
        touchFile(frame.canonicalPath());
    manifest.integrationGroups.push_back(group);

    writeNormalizationArtifact(group.normalizationCoefficientsArtifactPath.value(), group, group.lightFiles);
    manifest.integrationGroups.front().stacking.localNormalization.gridSize *= 2;

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(), [&](const ExecutionPlanDecision& decision) {
        return decision.stage == QStringLiteral("Lights Normalization");
    });
    ASSERT_NE(it, decisions.cend());
    EXPECT_EQ(it->action, ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsSkipPrecomputeForLocalNormalizationWithDrizzle)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsNormalization"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("drizzle_local_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("drizzle_local_b.fit")))};
    manifest.integrationGroups.push_back(group);

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(), [&](const ExecutionPlanDecision& decision) {
        return decision.stage == QStringLiteral("Lights Normalization");
    });
    ASSERT_NE(it, decisions.cend());
    EXPECT_EQ(it->action, ExecutionPlanDecision::Action::Skip);
    // The reason says what the drizzled stack's own fit leaves out, as the run log and the dialog do.
    EXPECT_TRUE(it->reason.contains(QStringLiteral("drizzled stack fits"))) << it->reason.toStdString();
    EXPECT_TRUE(it->reason.contains(QStringLiteral("reference strategy"))) << it->reason.toStdString();
    EXPECT_TRUE(it->reason.contains(QStringLiteral("PSF flux scale"))) << it->reason.toStdString();
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReuseDrizzlePixmapsAndInvalidateChangedSettings)
{
    ASSERT_NO_FATAL_FAILURE(openSession());
    const ImageIntegration::FrameHeaderReader reader = headerReader();

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("drizzle_a.fit"))),
                               FrameId(QDir(tempPath()).filePath(QStringLiteral("drizzle_b.fit")))};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("drizzle_pixmaps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    for (const auto& frame : group.lightFiles)
        ASSERT_NO_FATAL_FAILURE(writePlannerFrame(frame.canonicalPath()));

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest, std::nullopt, true,
        stage_utils::SourceDistortionProvenance::explicitNone()));
    ASSERT_TRUE(writeDrizzleDescriptorArtifacts(manifest, group, reader).valid());

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    auto actionForDrizzle = [&](const WorkflowManifest& candidate) {
        return actionOf(decisionsReadingHeaders(candidate, plan, reader), QStringLiteral("Lights Drizzle"));
    };

    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Reuse);
    // The sidecar names the kernel as the group spelled it then; another spelling of it is the same kernel.
    const QString kernel = manifest.integrationGroups.front().drizzle->kernel;
    manifest.integrationGroups.front().drizzle->kernel =
        kernel.toLower() == kernel ? kernel.toUpper() : kernel.toLower();
    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Reuse);
    manifest.integrationGroups.front().drizzle->kernel = QStringLiteral("Lanczos3");
    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Recompute);
    manifest.integrationGroups.front().drizzle->kernel = kernel;
    manifest.integrationGroups.front().drizzle->pixelFraction = 0.5f;
    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, CompactDescriptorsReuseAndInvalidateEveryMappingFingerprint)
{
    ASSERT_NO_FATAL_FAILURE(openSession());
    const ImageIntegration::FrameHeaderReader reader = headerReader();

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("compact_a.fit"))),
                               FrameId(QDir(tempPath()).filePath(QStringLiteral("compact_b.fit")))};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("compact_stacked_l.fit");
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("compact_drizzle_maps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    for (const auto& frame : group.lightFiles)
        ASSERT_NO_FATAL_FAILURE(writePlannerFrame(frame.canonicalPath()));

    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest, std::nullopt, true,
        stage_utils::SourceDistortionProvenance::explicitNone()));
    const DrizzleArtifacts artifacts = writeDrizzleDescriptorArtifacts(manifest, group, reader);
    ASSERT_TRUE(artifacts.valid());
    ASSERT_FALSE(artifacts.descriptors.empty());
    const QJsonObject original_metadata = artifacts.sidecar;
    const QString metadata_path = artifacts.sidecarPath();

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    auto actionForDrizzle = [&](const WorkflowManifest& candidate) {
        return actionOf(decisionsReadingHeaders(candidate, plan, reader), QStringLiteral("Lights Drizzle"));
    };
    auto expectMetadataInvalidation = [&](const QJsonObject& metadata) {
        ASSERT_TRUE(writeTestJsonFile(metadata_path, metadata));
        EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Recompute);
        ASSERT_TRUE(writeTestJsonFile(metadata_path, original_metadata));
    };
    auto expectDescriptorInvalidation = [&](const ImageIntegration::DrizzleDescriptorDocument& descriptor) {
        const QString descriptor_path = artifacts.descriptorPath(0);
        ASSERT_TRUE(publishDescriptor(descriptor_path, descriptor));
        EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Recompute);
        ASSERT_TRUE(publishDescriptor(descriptor_path, artifacts.descriptors.front()));
    };

    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Reuse);

    WorkflowManifest changed_scale = manifest;
    changed_scale.integrationGroups.front().drizzle->scale = 3;
    EXPECT_EQ(actionForDrizzle(changed_scale), ExecutionPlanDecision::Action::Recompute);

    QJsonObject changed_canvas = original_metadata;
    changed_canvas.insert(QStringLiteral("outputWidth"), static_cast<int>(kDrizzleOutputSide) + 1);
    expectMetadataInvalidation(changed_canvas);

    QJsonObject changed_transform = original_metadata;
    changed_transform.insert(QStringLiteral("transformsFingerprint"), QString(64, QLatin1Char('1')));
    expectMetadataInvalidation(changed_transform);

    QJsonObject changed_distortion = original_metadata;
    changed_distortion.insert(QStringLiteral("sourceDistortionFingerprint"), QString(64, QLatin1Char('2')));
    expectMetadataInvalidation(changed_distortion);

    QJsonObject changed_inputs = original_metadata;
    changed_inputs.insert(QStringLiteral("sourceInputsFingerprint"), QString(64, QLatin1Char('4')));
    expectMetadataInvalidation(changed_inputs);

    QJsonObject without_inputs = original_metadata;
    without_inputs.remove(QStringLiteral("sourceInputsFingerprint"));
    expectMetadataInvalidation(without_inputs);

    // A sidecar that does not state the descriptors' current family identity.
    QJsonObject without_family = original_metadata;
    without_family.remove(QStringLiteral("artifactFamily"));
    expectMetadataInvalidation(without_family);

    // A sidecar of another schema lists dense map files, which are never read.
    QJsonObject pixmap_sidecar = original_metadata;
    pixmap_sidecar.remove(QStringLiteral("artifactFamily"));
    pixmap_sidecar.insert(QStringLiteral("schemaVersion"),
                          static_cast<int>(ImageIntegration::schema::kPixmapSchemaVersion));
    QJsonArray pixmap_frames = pixmap_sidecar.value(QStringLiteral("frames")).toArray();
    for (qsizetype index = 0; index < pixmap_frames.size(); ++index)
    {
        QJsonObject frame = pixmap_frames.at(index).toObject();
        frame.remove(QStringLiteral("descriptorFile"));
        frame.insert(QStringLiteral("pixmapFile"), QStringLiteral("frame_%1.pixmap").arg(index));
        pixmap_frames.replace(index, frame);
    }
    pixmap_sidecar.insert(QStringLiteral("frames"), pixmap_frames);
    expectMetadataInvalidation(pixmap_sidecar);

    // A sidecar naming a descriptor that is not on disk.
    QJsonObject missing_descriptor = original_metadata;
    QJsonArray frames = missing_descriptor.value(QStringLiteral("frames")).toArray();
    ASSERT_FALSE(frames.isEmpty());
    QJsonObject frame = frames.at(0).toObject();
    frame.insert(QStringLiteral("descriptorFile"), QStringLiteral("absent.descriptor"));
    frames.replace(0, frame);
    missing_descriptor.insert(QStringLiteral("frames"), frames);
    expectMetadataInvalidation(missing_descriptor);

    // A descriptor whose own mapping no longer matches the registration.
    ImageIntegration::DrizzleDescriptorDocument changed_mapping = artifacts.descriptors.front();
    const an::Result<an::RegistrationTransform> shifted = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Affine,
                                         .sourceToReference = {1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}});
    ASSERT_TRUE(shifted.hasValue()) << shifted.status().message();
    changed_mapping.transform = *shifted;
    expectDescriptorInvalidation(changed_mapping);

    // A descriptor for another source extent.
    ImageIntegration::DrizzleDescriptorDocument changed_source_dimensions = artifacts.descriptors.front();
    ++changed_source_dimensions.sourceWidth;
    expectDescriptorInvalidation(changed_source_dimensions);

    // A descriptor the engine never identified.
    ImageIntegration::DrizzleDescriptorDocument without_identity = artifacts.descriptors.front();
    without_identity.engineIdentity.clear();
    expectDescriptorInvalidation(without_identity);

    WorkflowManifest dense_checkpoint = manifest;
    dense_checkpoint.integrationGroups.front().drizzle->mapPersistencePolicy =
        WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint;
    EXPECT_EQ(actionForDrizzle(dense_checkpoint), ExecutionPlanDecision::Action::Recompute);

    // Every invalidation above was undone, so the artifacts are reused again.
    EXPECT_EQ(actionForDrizzle(manifest), ExecutionPlanDecision::Action::Reuse);
}

TEST_F(ExecutionPlannerTest, InheritedSipValidFitsNoOpThenSameStampMutationReplansDrizzleAndStacking)
{
    ASSERT_NO_FATAL_FAILURE(openSession());
    const ImageIntegration::FrameHeaderReader reader = headerReader();

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|InheritedSip")};
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("planner_sip_a.fit"))),
                               FrameId(QDir(tempPath()).filePath(QStringLiteral("planner_sip_b.fit")))};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|InheritedSip")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("planner_sip_stack.fit");
    group.skipIfExists = true;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("planner_sip_maps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    for (const auto& frame : group.lightFiles)
    {
        ASSERT_NO_FATAL_FAILURE(writePlannerFrame(frame.canonicalPath(), 0.001));
        const std::optional<ImageIntegration::FrameHeaderFacts> header = reader(frame.canonicalPath());
        ASSERT_TRUE(header.has_value());
        ASSERT_TRUE(header->distortion.has_value()) << "The frame's header must state a distortion to inherit.";
    }

    // The checkpoint rows hold records that inherit each frame's own distortion.
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(
        QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")), manifest));
    const DrizzleArtifacts artifacts = writeDrizzleDescriptorArtifacts(manifest, group, reader);
    ASSERT_TRUE(artifacts.valid());
    for (const auto& descriptor : artifacts.descriptors)
    {
        ASSERT_EQ(descriptor.sourceDistortion.policy, stage_utils::SourceDistortionPolicy::InheritMetadata);
        ASSERT_TRUE(descriptor.resolvedSourceDistortion.has_value());
    }
    const QString original_inputs_fingerprint = computeFrameFileStampFingerprint(group.lightFiles);

    const QString original_stack_hash =
        computeStackedLightArtifactHash(manifest, group, original_inputs_fingerprint, artifacts.transformsFingerprint(),
                                        artifacts.sourceDistortionFingerprint());
    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    touchFile(output_path);
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(output_path, original_stack_hash);
        ASSERT_TRUE(tracker.isArtifactValid(output_path, original_stack_hash));
    }

    const auto original_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest, {}, reader);
    ASSERT_TRUE(original_snapshot);
    ASSERT_TRUE(original_snapshot->matchesCurrentFacts());

    IntegrationDataModel model;
    ExecutionPlanner planner(model);
    const ExecutionPlan no_op_plan = planner.buildPlan(manifest, *original_snapshot);
    EXPECT_EQ(no_op_plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(no_op_plan.isEmpty());

    ExecutionPlan selected_plan;
    selected_plan.mode = ExecutionMode::Incremental;
    selected_plan.integrationGroupsToProcess.push_back(group.groupKey);
    const auto reusable_decisions = buildExecutionPlanDecisions(manifest, selected_plan, *original_snapshot);
    EXPECT_EQ(actionOf(reusable_decisions, QStringLiteral("Lights Drizzle")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(reusable_decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Reuse);

    const FrameId mutated_frame = group.lightFiles.front();
    const QFileInfo original_info(mutated_frame.canonicalPath());
    const QDateTime original_modified = original_info.lastModified();
    const qint64 original_size = original_info.size();
    ASSERT_NO_FATAL_FAILURE(writePlannerFrame(mutated_frame.canonicalPath(), 0.002));
    QFile replacement(mutated_frame.canonicalPath());
    ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacement.setFileTime(original_modified, QFileDevice::FileModificationTime));
    replacement.close();
    ASSERT_EQ(QFileInfo(mutated_frame.canonicalPath()).size(), original_size);
    ASSERT_NE(computeFrameFileStampFingerprint(group.lightFiles), original_inputs_fingerprint);
    EXPECT_FALSE(original_snapshot->matchesCurrentFacts());

    {
        ArtifactTracker tracker(tempPath());
        ASSERT_TRUE(tracker.isArtifactValid(output_path, original_stack_hash));
    }

    const auto mutated_snapshot = ExecutionPreparationSnapshot::build(manifest, manifest, {}, reader);
    ASSERT_TRUE(mutated_snapshot);
    ASSERT_TRUE(mutated_snapshot->matchesCurrentFacts());
    const auto* mutated_geometry = mutated_snapshot->sourceGeometry(mutated_frame);
    ASSERT_NE(mutated_geometry, nullptr);
    EXPECT_NE(mutated_geometry->inheritedSourceDistortionFingerprint,
              stage_utils::fingerprintSourceDistortionHex(stage_utils::SourceDistortionPolicy::InheritMetadata,
                                                          artifacts.descriptors.front().resolvedSourceDistortion));
    const ExecutionPlan plan = planner.buildPlan(manifest, *mutated_snapshot);
    EXPECT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);
    EXPECT_EQ(plan.integrationGroupsToProcess.front(), group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, *mutated_snapshot);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Drizzle")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, CaseAndSeparatorVariantRowsReuseDrizzleWithoutReregistration)
{
    ASSERT_NO_FATAL_FAILURE(openSession());
    const ImageIntegration::FrameHeaderReader reader = headerReader();

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|AliasIdentity")};
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("AliasIdentity/Light_A.fit"))),
                               FrameId(QDir(tempPath()).filePath(QStringLiteral("AliasIdentity/Light_B.fit")))};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|AliasIdentity")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("other_identity_stack.fit");
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("other_identity_maps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    for (const auto& frame : group.lightFiles)
        ASSERT_NO_FATAL_FAILURE(writePlannerFrame(frame.canonicalPath()));

    const QString metrics_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(metrics_path, manifest, std::nullopt, true,
                                                       stage_utils::SourceDistortionProvenance::explicitNone()));
    ASSERT_TRUE(writeDrizzleDescriptorArtifacts(manifest, group, reader).valid());
    ASSERT_TRUE(rewriteRegistrationMetricFrameIdsWithOtherCaseAndSeparators(metrics_path, registration.groupKey,
                                                                            registration.lightFiles));

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);
    const auto decisions = decisionsReadingHeaders(manifest, plan, reader);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Registration")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Drizzle")), ExecutionPlanDecision::Action::Reuse);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReusePersistedCalibratedLights)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsStacking")};
    manifest.calibrateLightsInMemory = false;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("cal_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("cal_b.fit")))};
    manifest.integrationGroups.push_back(group);

    for (const auto& light : group.lightFiles)
    {
        touchFile(light.canonicalPath());
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        params.settings.pedestal = 100.0;
        ASSERT_TRUE(params.calibrates());
        manifest.lightsCalibration.push_back(params);
    }

    {
        ArtifactTracker tracker(tempPath());
        for (const auto& params : manifest.lightsCalibration)
        {
            const QString output = calibratedLightOutputPathFor(tempPath(), params.lightPath);
            touchFile(output);
            tracker.recordArtifact(output, computeCalibratedLightArtifactHash(params));
        }
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(), [&](const ExecutionPlanDecision& decision) {
        return decision.stage == QStringLiteral("Lights Calibration");
    });
    ASSERT_NE(it, decisions.cend());
    EXPECT_EQ(it->action, ExecutionPlanDecision::Action::Reuse);
}

// Lights with no master and no pedestal have nothing to calibrate them with: the stage uses them as they are and makes
// no calibrated copy, so the plan says so rather than that persisted calibrated lights are reused.
TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsSayLightsWithNothingToCalibrateThemAreUsedAsTheyAre)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsCalibration"), QStringLiteral("LightsStacking")};
    manifest.calibrateLightsInMemory = false;

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("as_is_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("as_is_b.fit")))};
    manifest.integrationGroups.push_back(group);
    for (const auto& light : group.lightFiles)
    {
        touchFile(light.canonicalPath());
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        ASSERT_FALSE(params.calibrates());
        manifest.lightsCalibration.push_back(params);
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, manifest);
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(), [&](const ExecutionPlanDecision& decision) {
        return decision.stage == QStringLiteral("Lights Calibration");
    });
    ASSERT_NE(it, decisions.cend());
    EXPECT_EQ(it->action, ExecutionPlanDecision::Action::Skip);
    EXPECT_TRUE(it->reason.contains(QStringLiteral("Nothing to calibrate with"))) << it->reason.toStdString();
    EXPECT_FALSE(it->reason.contains(QStringLiteral("Persisted calibrated light frames"))) << it->reason.toStdString();
}

TEST_F(ExecutionPlannerTest, CalibratedLightHashTracksSameStampCalibrationMasterReplacement)
{
    const QString light_path = QDir(tempPath()).filePath(QStringLiteral("identity_light.fit"));
    const QString bias_path = QDir(tempPath()).filePath(QStringLiteral("identity_bias.fit"));
    ASSERT_NO_FATAL_FAILURE(writePlannerFrame(light_path, 0.4));
    ASSERT_NO_FATAL_FAILURE(writePlannerFrame(bias_path, 0.1));

    WorkflowManifest::LightsCalibrationParams params;
    params.lightPath = FrameId(light_path);
    params.biasPath = FrameId(bias_path);
    const QString original_hash = computeCalibratedLightArtifactHash(params);
    ASSERT_FALSE(original_hash.isEmpty());

    const QFileInfo original_bias_info(bias_path);
    const QDateTime original_modified = original_bias_info.lastModified();
    const qint64 original_size = original_bias_info.size();
    // With its size and modification time kept, the replacement differs only in the change the file system stamps
    // on it, and that stamp moves once per tick of the system clock: 15.6 ms on Windows unless a program asks for a
    // finer one. A replacement within the original's tick is the same change to it, so the case waits two ticks.
    std::this_thread::sleep_for(std::chrono::milliseconds(32));
    ASSERT_NO_FATAL_FAILURE(writePlannerFrame(bias_path, 0.2));
    QFile replacement(bias_path);
    ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
    ASSERT_TRUE(replacement.setFileTime(original_modified, QFileDevice::FileModificationTime));
    replacement.close();
    ASSERT_EQ(QFileInfo(bias_path).size(), original_size);

    EXPECT_NE(computeCalibratedLightArtifactHash(params), original_hash);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReusePersistedStackAndInvalidateChangedSettings)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.skipIfExists = true;
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("stack_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("stack_b.fit")))};
    for (const auto& frame : group.lightFiles)
        touchFile(frame.canonicalPath());
    manifest.integrationGroups.push_back(group);

    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    touchFile(output_path);
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(output_path, computeStackedLightArtifactHash(
                                                manifest, group, computeFrameFileStampFingerprint(group.lightFiles)));
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    auto actionForStacking = [&](const WorkflowManifest& candidate) {
        const auto decisions = buildExecutionPlanDecisions(candidate, plan, candidate);
        return actionOf(decisions, QStringLiteral("Lights Stacking"));
    };

    EXPECT_EQ(actionForStacking(manifest), ExecutionPlanDecision::Action::Reuse);
    manifest.integrationGroups.front().stacking.sigmaHigh += 0.5;
    EXPECT_EQ(actionForStacking(manifest), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReuseDrizzleStackWithChromaticAberrationInputs)
{
    ASSERT_NO_FATAL_FAILURE(openSession());
    const ImageIntegration::FrameHeaderReader reader = headerReader();

    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.settings.chromaticAberration.enabled = true;
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("light_a.fit"))),
                               FrameId(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")))};
    for (const auto& frame : registration.lightFiles)
    {
        ASSERT_NO_FATAL_FAILURE(writePlannerFrame(frame.canonicalPath()));
    }
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.skipIfExists = true;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("pixmaps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    const QString metrics_path = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    ASSERT_TRUE(createRegistrationMetricsDbForManifest(metrics_path, manifest, std::nullopt, true,
                                                       stage_utils::SourceDistortionProvenance::explicitNone()));
    const DrizzleArtifacts artifacts = writeDrizzleDescriptorArtifacts(manifest, group, reader);
    ASSERT_TRUE(artifacts.valid());

    std::vector<FrameId> chromatic_aberration_inputs;
    chromatic_aberration_inputs.reserve(group.lightFiles.size());
    for (const auto& frame : group.lightFiles)
    {
        const QString path = chromaticAberrationLightOutputPathFor(tempPath(), frame);
        touchFile(path);
        chromatic_aberration_inputs.emplace_back(path);
    }

    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    touchFile(output_path);
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(output_path,
                               computeStackedLightArtifactHash(
                                   manifest, group, computeFrameFileStampFingerprint(chromatic_aberration_inputs),
                                   artifacts.transformsFingerprint(), artifacts.sourceDistortionFingerprint()));
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.integrationGroupsToProcess.push_back(group.groupKey);

    const auto decisions = decisionsReadingHeaders(manifest, plan, reader);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Drizzle")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Reuse);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsRecomputeDrizzleAndStackingWhenRegistrationRecomputes)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsRegistration"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("light_a.fit")))};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|L|M42")}};
    group.registrationGroupKey = registration.groupKey;
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.skipIfExists = true;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    group.drizzlePixmapArtifactDir = QDir(tempPath()).filePath(QStringLiteral("pixmaps"));
    group.lightFiles = registration.lightFiles;
    manifest.integrationGroups.push_back(group);

    touchFile(QDir(tempPath()).filePath(group.outputFilename));

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Full;

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, std::nullopt);
    const auto actionFor = [&](const QString& stage) -> std::optional<ExecutionPlanDecision::Action> {
        const auto it = std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == stage && decision.groupKey == group.groupKey.toCompactString();
        });
        if (it == decisions.end())
        {
            return std::nullopt;
        }
        return it->action;
    };

    ASSERT_TRUE(actionFor(QStringLiteral("Lights Drizzle")).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Lights Drizzle")).value(), ExecutionPlanDecision::Action::Recompute);
    ASSERT_TRUE(actionFor(QStringLiteral("Lights Stacking")).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Lights Stacking")).value(), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsRecomputeCalibrationConsumersWhenMasterRecomputes)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("Bias"), QStringLiteral("Dark"), QStringLiteral("Flat")};

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group bias;
    bias.groupKey = GroupKey{QStringLiteral("bias|100x100")};
    bias.outputFilename = QStringLiteral("master_bias.fit");
    bias.skipIfExists = true;
    bias.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("bias_a.fit")))};
    biasStage.groups.push_back(bias);
    manifest.biasStage = biasStage;

    const FrameId biasMaster(QDir(tempPath()).filePath(bias.outputFilename));

    WorkflowManifest::CalibrationStage darkStage;
    darkStage.frameType = QStringLiteral("DARK");
    WorkflowManifest::CalibrationStage::Group dark;
    dark.groupKey = GroupKey{QStringLiteral("dark|100x100")};
    dark.outputFilename = QStringLiteral("master_dark.fit");
    dark.skipIfExists = true;
    dark.biasMaster = biasMaster;
    dark.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("dark_a.fit")))};
    darkStage.groups.push_back(dark);
    manifest.darkStage = darkStage;

    const FrameId darkMaster(QDir(tempPath()).filePath(dark.outputFilename));

    WorkflowManifest::CalibrationStage flatStage;
    flatStage.frameType = QStringLiteral("FLAT");
    WorkflowManifest::CalibrationStage::Group flat;
    flat.groupKey = GroupKey{QStringLiteral("flat|100x100")};
    flat.outputFilename = QStringLiteral("master_flat.fit");
    flat.skipIfExists = true;
    flat.darkMaster = darkMaster;
    flat.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("flat_a.fit")))};
    flatStage.groups.push_back(flat);
    manifest.flatStage = flatStage;

    touchFile(QDir(tempPath()).filePath(dark.outputFilename));
    touchFile(QDir(tempPath()).filePath(flat.outputFilename));

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Full;

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, std::nullopt);
    const auto actionFor = [&](const QString& stage,
                               const QString& groupKey) -> std::optional<ExecutionPlanDecision::Action> {
        const auto it = std::find_if(decisions.begin(), decisions.end(), [&](const ExecutionPlanDecision& decision) {
            return decision.stage == stage && decision.groupKey == groupKey;
        });
        if (it == decisions.end())
        {
            return std::nullopt;
        }
        return it->action;
    };

    ASSERT_TRUE(actionFor(QStringLiteral("Bias"), bias.groupKey.toCompactString()).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Bias"), bias.groupKey.toCompactString()).value(),
              ExecutionPlanDecision::Action::Recompute);
    ASSERT_TRUE(actionFor(QStringLiteral("Dark"), dark.groupKey.toCompactString()).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Dark"), dark.groupKey.toCompactString()).value(),
              ExecutionPlanDecision::Action::Recompute);
    ASSERT_TRUE(actionFor(QStringLiteral("Flat"), flat.groupKey.toCompactString()).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Flat"), flat.groupKey.toCompactString()).value(),
              ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsReusePersistedMasterAndInvalidateChangedInputs)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("Bias")};

    WorkflowManifest::CalibrationStage stage;
    stage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|100x100")};
    group.outputFilename = QStringLiteral("master_bias.fit");
    group.skipIfExists = true;
    group.inputFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("bias_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("bias_b.fit")))};
    for (const auto& frame : group.inputFiles)
        touchFile(frame.canonicalPath());
    stage.groups.push_back(group);
    manifest.biasStage = stage;

    const QString output_path = QDir(tempPath()).filePath(group.outputFilename);
    touchFile(output_path);
    {
        ArtifactTracker tracker(tempPath());
        tracker.recordArtifact(output_path, computeCalibrationGroupHash(manifest, group));
    }

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Incremental;
    plan.biasGroupsToProcess.push_back(group.groupKey);

    auto actionForBias = [&](const WorkflowManifest& candidate) {
        const auto decisions = buildExecutionPlanDecisions(candidate, plan, candidate);
        return actionOf(decisions, QStringLiteral("Bias"));
    };

    EXPECT_EQ(actionForBias(manifest), ExecutionPlanDecision::Action::Reuse);

    QFile changed_input(group.inputFiles.front().canonicalPath());
    ASSERT_TRUE(changed_input.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(changed_input.write("changed"), 7);
    changed_input.close();
    EXPECT_EQ(actionForBias(manifest), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, ExecutionPlanDecisionsExposeSkippedOptionalStages)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsNormalization"), QStringLiteral("LightsDrizzle"),
                              QStringLiteral("LightsStacking")};

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("L|M42")}};
    group.registrationGroupKey = GroupKey{QStringLiteral("R|M42")};
    group.outputFilename = QStringLiteral("stacked_l.fit");
    group.stacking.normalizationMode = NormalizationMode::None;
    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = false;
    group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("light_a.fit"))),
                        FrameId(QDir(tempPath()).filePath(QStringLiteral("light_b.fit")))};
    manifest.integrationGroups.push_back(group);

    ExecutionPlan plan;
    plan.mode = ExecutionMode::Full;

    const auto decisions = buildExecutionPlanDecisions(manifest, plan, std::nullopt);

    auto actionFor = [&](const QString& stage) -> std::optional<ExecutionPlanDecision::Action> {
        for (const auto& decision : decisions)
        {
            if (decision.stage == stage)
            {
                return decision.action;
            }
        }
        return std::nullopt;
    };

    ASSERT_TRUE(actionFor(QStringLiteral("Lights Normalization")).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Lights Normalization")).value(), ExecutionPlanDecision::Action::Skip);
    ASSERT_TRUE(actionFor(QStringLiteral("Lights Drizzle")).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Lights Drizzle")).value(), ExecutionPlanDecision::Action::Skip);
    ASSERT_TRUE(actionFor(QStringLiteral("Lights Stacking")).has_value());
    EXPECT_EQ(actionFor(QStringLiteral("Lights Stacking")).value(), ExecutionPlanDecision::Action::Recompute);
}

TEST_F(ExecutionPlannerTest, AlignmentPlanPublishesTheSameTypedMaterializationPolicyUsedByRouting)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();
    manifest.stageSequence = {QStringLiteral("LightsAlignment"), QStringLiteral("LightsStacking")};

    WorkflowManifest::RegistrationGroup registration_group;
    registration_group.groupKey = GroupKey{QStringLiteral("R|AlignmentPlan")};
    registration_group.settings.alignmentMode = QStringLiteral("standard");
    registration_group.settings.interpolation = QStringLiteral("Bilinear");
    registration_group.lightFiles = {FrameId(QDir(tempPath()).filePath(QStringLiteral("alignment_plan_a.fit"))),
                                     FrameId(QDir(tempPath()).filePath(QStringLiteral("alignment_plan_b.fit")))};
    registration_group.referenceFrame = registration_group.lightFiles.front();
    manifest.registrationGroups.push_back(registration_group);

    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("L|AlignmentPlan")}};
    group.registrationGroupKey = registration_group.groupKey;
    group.outputFilename = QStringLiteral("alignment_plan_stack.fit");
    group.lightFiles = registration_group.lightFiles;
    group.stacking.stackMethod = QStringLiteral("Median");
    group.stacking.normalizationMode = NormalizationMode::None;
    manifest.integrationGroups.push_back(group);

    // The stacking stage's routing for a group that keeps its registered images: they are written, and the plan says
    // why in the stage's own words.
    ExecutionPlan plan;
    plan.mode = ExecutionMode::Full;
    const auto decisions = buildExecutionPlanDecisions(manifest, plan, std::nullopt);
    const auto alignment_it =
        std::find_if(decisions.cbegin(), decisions.cend(), [](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Alignment");
        });

    ASSERT_NE(alignment_it, decisions.cend());
    ASSERT_TRUE(alignment_it->transformOnReadDecision.has_value());
    const ImageIntegration::TransformOnReadRouting& published = alignment_it->transformOnReadDecision.value();
    EXPECT_FALSE(published.usesTransformOnRead());
    EXPECT_EQ(published.reason, QStringLiteral("registered_output_retention"));
    EXPECT_FALSE(published.detail.trimmed().isEmpty());
    EXPECT_EQ(alignment_it->action, ExecutionPlanDecision::Action::Recompute);
    EXPECT_FALSE(alignment_it->reason.trimmed().isEmpty());
    EXPECT_TRUE(alignment_it->reason.contains(published.reason));
    EXPECT_TRUE(alignment_it->reason.contains(published.detail));

    // When the group's registered images are discardable the stack reads the lights warped, no registered image is
    // needed, and the plan still says why.
    manifest.integrationGroups.front().stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    const auto warped_decisions = buildExecutionPlanDecisions(manifest, plan, std::nullopt);
    const auto warped_alignment =
        std::find_if(warped_decisions.cbegin(), warped_decisions.cend(), [](const ExecutionPlanDecision& decision) {
            return decision.stage == QStringLiteral("Lights Alignment");
        });
    ASSERT_NE(warped_alignment, warped_decisions.cend());
    ASSERT_TRUE(warped_alignment->transformOnReadDecision.has_value());
    EXPECT_TRUE(warped_alignment->transformOnReadDecision->usesTransformOnRead());
    EXPECT_EQ(warped_alignment->transformOnReadDecision->reason, QStringLiteral("registration_validation_deferred"));
    EXPECT_EQ(warped_alignment->action, ExecutionPlanDecision::Action::Skip);
    EXPECT_TRUE(warped_alignment->reason.contains(warped_alignment->transformOnReadDecision->reason));
}

TEST_F(ExecutionPlannerTest, MaterializationPolicyUsesEverySiblingInTheRegistrationGroup)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = tempPath();

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("R|MixedFormats")};
    registration.lightFiles = {
        FrameId(QDir(tempPath()).filePath(QStringLiteral("mixed_only_integration.fit"))),
        FrameId(QDir(tempPath()).filePath(QStringLiteral("mixed_sibling.png"))),
    };
    registration.referenceFrame = registration.lightFiles.front();
    manifest.registrationGroups.push_back(registration);

    EXPECT_TRUE(registrationGroupMaterializationUsesFits(manifest, manifest.registrationGroups.front()));
    const QString mixed_fingerprint =
        registrationGroupMaterializationContractFingerprint(manifest, manifest.registrationGroups.front());
    ASSERT_EQ(mixed_fingerprint.size(), 64);

    manifest.registrationGroups.front().lightFiles.pop_back();
    EXPECT_FALSE(registrationGroupMaterializationUsesFits(manifest, manifest.registrationGroups.front()));
    const QString fits_only_fingerprint =
        registrationGroupMaterializationContractFingerprint(manifest, manifest.registrationGroups.front());
    EXPECT_NE(fits_only_fingerprint, mixed_fingerprint);
}

namespace
{
/// The reason of the first decision for `stage`; a missing decision is a failure, answered as no reason.
QString reasonOf(const std::vector<ExecutionPlanDecision>& decisions, const QString& stage)
{
    const auto it = std::find_if(decisions.cbegin(), decisions.cend(),
                                 [&](const ExecutionPlanDecision& decision) { return decision.stage == stage; });
    EXPECT_NE(it, decisions.cend()) << "No decision for " << stage.toStdString();
    return it != decisions.cend() ? it->reason : QString();
}

/// A full run of a light group that kept its registered images: the model it was planned from, the manifest it ran,
/// and what it left on disk - the registered lights, the registration's records of them and the coefficients the
/// normalization measured on them.
class CompletedRunKeepingRegisteredLights
{
public:
    explicit CompletedRunKeepingRegisteredLights(const QString& directory)
    {
        m_model.setOutputDirectory(directory);
        m_model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("kept_light_a.fit")),
                                             makeFrame(FrameType::Light, QStringLiteral("kept_light_b.fit"))});
        m_previous = ManifestBuilder(m_model).build();
        EXPECT_EQ(m_previous.integrationGroups.size(), 1u);
        if (m_previous.integrationGroups.empty())
        {
            return;
        }

        const WorkflowManifest::IntegrationGroup& group = m_previous.integrationGroups.front();
        std::vector<FrameId> registered;
        for (const FrameId& light : group.lightFiles)
        {
            const QString path = registeredLightPathFor(directory, group, light.canonicalPath());
            touchFile(path);
            registered.emplace_back(path);
        }
        EXPECT_TRUE(createRegistrationMetricsDbForManifest(
            QDir(directory).filePath(QStringLiteral("registration_metrics.db")), m_previous));
        EXPECT_TRUE(group.normalizationCoefficientsArtifactPath.has_value());
        if (group.normalizationCoefficientsArtifactPath.has_value())
        {
            writeNormalizationArtifact(*group.normalizationCoefficientsArtifactPath, group, registered);
        }
    }

    [[nodiscard]] const WorkflowManifest& previous() const noexcept
    {
        return m_previous;
    }

    /// The manifest the model states once `change` is made to the group's stacking settings.
    [[nodiscard]] WorkflowManifest manifestAfter(const std::function<void(StackingSettings&)>& change)
    {
        const GroupKey key = m_model.integrationGroups().front();
        StackingSettings settings;
        const auto& stored = m_model.stackingSettings(FrameType::Light);
        if (const auto found = stored.find(key); found != stored.end())
        {
            settings = found->second;
        }
        change(settings);
        m_model.setStackingSettings(FrameType::Light, key, settings);
        return ManifestBuilder(m_model).build();
    }

private:
    IntegrationDataModel m_model;
    WorkflowManifest m_previous;
};
} // namespace

// The stacking method is read by the stack alone: after a full run, changing it stacks the group again and reuses its
// registered lights and its normalization coefficients.
TEST_F(ExecutionPlannerTest, AChangedStackingMethodRunsTheStackAndReusesTheAlignedAndNormalizedLights)
{
    CompletedRunKeepingRegisteredLights run(tempPath());
    ASSERT_FALSE(HasFailure());
    const WorkflowManifest& previous = run.previous();
    const WorkflowManifest current =
        run.manifestAfter([](StackingSettings& settings) { settings.stackMethod = QStringLiteral("Median"); });

    const ExecutionPlan plan = ExecutionPlanner().buildPlan(current, previous);
    ASSERT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());
    ASSERT_EQ(plan.integrationGroupsToProcess.size(), 1u);

    const auto decisions = buildExecutionPlanDecisions(current, plan, previous);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Registration")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Alignment")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Normalization")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Stacking")), QStringLiteral("Stacking settings changed"));
}

// A normalization setting is read by the normalization and the stack: changing it measures the coefficients and stacks
// again, and the registered lights are reused.
TEST_F(ExecutionPlannerTest, AChangedNormalizationSettingRunsTheNormalizationAndTheStack)
{
    CompletedRunKeepingRegisteredLights run(tempPath());
    ASSERT_FALSE(HasFailure());
    const WorkflowManifest& previous = run.previous();
    ASSERT_EQ(previous.integrationGroups.front().stacking.scaleEstimator, ScaleEstimatorMethod::IKSS);
    const WorkflowManifest current =
        run.manifestAfter([](StackingSettings& settings) { settings.scaleEstimator = ScaleEstimatorMethod::MAD; });

    const ExecutionPlan plan = ExecutionPlanner().buildPlan(current, previous);
    ASSERT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());

    const auto decisions = buildExecutionPlanDecisions(current, plan, previous);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Alignment")), ExecutionPlanDecision::Action::Reuse);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Normalization")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Normalization")),
              QStringLiteral("Normalization settings changed"));
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Recompute);
}

// Turning drizzle on moves the plane the normalization measures on and asks for the drizzle: both run with the stack,
// and the registered lights a drizzled stack does not read are not asked for.
TEST_F(ExecutionPlannerTest, TurningDrizzleOnRunsTheNormalizationTheDrizzleAndTheStack)
{
    CompletedRunKeepingRegisteredLights run(tempPath());
    ASSERT_FALSE(HasFailure());
    const WorkflowManifest& previous = run.previous();
    const WorkflowManifest current =
        run.manifestAfter([](StackingSettings& settings) { settings.drizzle.enabled = true; });

    const ExecutionPlan plan = ExecutionPlanner().buildPlan(current, previous);
    ASSERT_EQ(plan.mode, ExecutionMode::Incremental);
    EXPECT_TRUE(plan.registrationGroupsToProcess.empty());

    // A group that only drizzles plans no alignment at all.
    const auto decisions = buildExecutionPlanDecisions(current, plan, previous);
    EXPECT_TRUE(std::none_of(decisions.cbegin(), decisions.cend(), [](const ExecutionPlanDecision& decision) {
        return decision.stage == QStringLiteral("Lights Alignment");
    }));
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Normalization")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Normalization")), QStringLiteral("Drizzle settings changed"));
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Drizzle")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Drizzle")), QStringLiteral("Drizzle settings changed"));
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Recompute);
}

// A stage that runs again for a reason of its own says that reason, not the change of a setting it does not read:
// with a registered light gone, the alignment writes it again and the normalization measures again, while the stack
// runs for the method.
TEST_F(ExecutionPlannerTest, AStageThatRunsForAReasonOfItsOwnSaysIt)
{
    CompletedRunKeepingRegisteredLights run(tempPath());
    ASSERT_FALSE(HasFailure());
    const WorkflowManifest& previous = run.previous();
    const WorkflowManifest current =
        run.manifestAfter([](StackingSettings& settings) { settings.stackMethod = QStringLiteral("Median"); });
    const WorkflowManifest::IntegrationGroup& group = current.integrationGroups.front();
    ASSERT_TRUE(QFile::remove(registeredLightPathFor(tempPath(), group, group.lightFiles.front().canonicalPath())));

    const ExecutionPlan plan = ExecutionPlanner().buildPlan(current, previous);
    ASSERT_EQ(plan.mode, ExecutionMode::Incremental);
    const auto decisions = buildExecutionPlanDecisions(current, plan, previous);
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Alignment")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_TRUE(reasonOf(decisions, QStringLiteral("Lights Alignment"))
                    .startsWith(QStringLiteral("Registered light outputs are missing or changed")))
        << reasonOf(decisions, QStringLiteral("Lights Alignment")).toStdString();
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Normalization")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Normalization")),
              QStringLiteral("Alignment outputs will be recomputed"));
    EXPECT_EQ(actionOf(decisions, QStringLiteral("Lights Stacking")), ExecutionPlanDecision::Action::Recompute);
    EXPECT_EQ(reasonOf(decisions, QStringLiteral("Lights Stacking")), QStringLiteral("Stacking settings changed"));
}
