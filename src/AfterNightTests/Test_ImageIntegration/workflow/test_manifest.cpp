// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingEngine.h"
#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/core/CalibrationMasterDefaults.h"
#include "processes/ImageIntegration/core/CometStackingMode.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestDiff.h"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/WorkflowRawLoadPolicy.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "Test_ImageIntegration/ImportSession.h"

#include <AfterNightEngine/Algorithms/Demosaic/CfaWhiteBalance.h>
#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/IO/RawCodec.h>

namespace
{
/// The application context the manifest builder reads its global settings through, holding the settings in memory and
/// lending no other service. Installed for its own lifetime only.
class ScopedGlobalSettingsContext final : public an::ui::IAppContext, public an::ui::IGlobalSettingsProvider
{
public:
    ScopedGlobalSettingsContext()
    {
        m_previous = an::ui::appContext();
        // setAppContext installs over nothing, so whatever is installed - the suite's own - is taken down first.
        an::ui::clearAppContext();
        an::ui::setAppContext(this);
    }

    ~ScopedGlobalSettingsContext() override
    {
        an::ui::clearAppContext();
        if (m_previous != nullptr)
        {
            an::ui::setAppContext(m_previous);
        }
    }

    ScopedGlobalSettingsContext(const ScopedGlobalSettingsContext&) = delete;
    ScopedGlobalSettingsContext& operator=(const ScopedGlobalSettingsContext&) = delete;

    bool upsert(const QString& key, const QString& value) override
    {
        m_values.insert(key, value);
        return true;
    }

    [[nodiscard]] QString select(const QString& key, const QString& defaultValue) override
    {
        return m_values.value(key, defaultValue);
    }

    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        const auto it = m_values.constFind(key);
        return it == m_values.cend() ? std::nullopt : std::optional<QString>(it.value());
    }

    [[nodiscard]] an::ui::IIconProviderPtr getIconProvider() override
    {
        return {};
    }
    [[nodiscard]] an::ui::IDialogService* getDialogService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IGlobalSettingsProvider* getGlobalSettings() override
    {
        return this;
    }
    [[nodiscard]] an::ui::IInterfaceSettingsRuntimeService* getInterfaceSettingsRuntimeService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessSettingsProvider* getProcessSettings() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IPathMemoryService* getPathMemoryService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IFileDialogService* getFileDialogService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewRegistry* getViewRegistry() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IAppEventBus* getAppEventBus() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IProcessRegistry* getProcessRegistry() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IOutputIntentProvider* getOutputIntentProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IRecentFilesService* getRecentFilesService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IThumbnailPathProvider* getThumbnailPathProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IViewControlManager* getViewControlManager() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IStatusBarService* getStatusBarService() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IActionStateManager* getActionStateManager() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IEngineSessionProvider* getEngineSessionProvider() override
    {
        return nullptr;
    }
    [[nodiscard]] an::ui::IRawOpenPolicyService* getRawOpenPolicyService() override
    {
        return nullptr;
    }
    [[nodiscard]] QWidget* getMainWindow() override
    {
        return nullptr;
    }

private:
    an::ui::IAppContext* m_previous = nullptr;
    QHash<QString, QString> m_values;
};

FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size, double exposure, const QString& filter,
                    const QString& object, const QString& session, std::optional<double> temperature = 0.0,
                    std::optional<int> iso_gain = 100, std::optional<int> offset = 10, const QString& custom_tag = {},
                    const std::optional<QDateTime>& captured_at_utc = std::nullopt)
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    const QDateTime captured_at = captured_at_utc.value_or(QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC));
    FrameMetadata metadata(type, size, exposure, temperature, iso_gain, offset, filter, object, captured_at.toUTC(),
                           QStringLiteral("Default"));

    return FrameInfo(FrameId(path), path, metadata, session, custom_tag);
}

WorkflowManifest makeManifestWithFrames(const std::vector<FrameId>& frames)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("D:/Output");

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.resolution = QStringLiteral("100x100");
    registration.object = QStringLiteral("M42");
    registration.lightFiles = frames;
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|M42|L")}};
    integration.outputFilename = QStringLiteral("stacked_M42_L.fit");
    integration.resolution = QStringLiteral("100x100");
    integration.object = QStringLiteral("M42");
    integration.filter = QStringLiteral("L");
    integration.exposure = QStringLiteral("60");
    integration.lightFiles = frames;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    return manifest;
}

/// Writes a small light frame of the given size as a capture program writes one and answers its path.
QString writeManifestLightFrame(const QString& directory, const QString& filename, const std::uint32_t width = 16u,
                                const std::uint32_t height = 16u)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = "Light Frame";
    facts.exposureSeconds = 60.0;

    const QString path = QDir(directory).filePath(filename);
    std::string error;
    if (!an::tests::fixtures::writeAcquisitionFrame(
            an::tests::fixtures::acquisitionFrame(filename.toStdString(), facts, width, height),
            std::filesystem::path(path.toStdWString()), error))
    {
        ADD_FAILURE() << "Could not write " << path.toStdString() << ": " << error;
    }
    return path;
}

RegistrationSettings::CometTrackingSettings makeManifestCometSettings(const std::vector<FrameId>& frames)
{
    RegistrationSettings::CometTrackingSettings settings;
    settings.observations = {
        RegistrationSettings::CometTrackingObservation{frames.at(0).canonicalPath(), QPointF(7.0, 8.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{frames.at(1).canonicalPath(), QPointF(10.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
    };
    return settings;
}

WorkflowManifest makePureCometManifest(const QString& directory,
                                       const QString& comet_stacking_mode = QStringLiteral("Comet"))
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(directory).filePath(QStringLiteral("output"));

    const std::vector<FrameId> lights = {
        FrameId(writeManifestLightFrame(directory, QStringLiteral("comet_manifest_a.fit"))),
        FrameId(writeManifestLightFrame(directory, QStringLiteral("comet_manifest_b.fit"))),
    };

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|16x16|CometManifest")};
    registration.resolution = QStringLiteral("16x16");
    registration.object = QStringLiteral("CometManifest");
    registration.lightFiles = lights;
    registration.referenceFrame = lights.front();
    registration.settings.registrationMethod = QStringLiteral("comet_tracking");
    registration.settings.cometTracking = makeManifestCometSettings(lights);
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|16x16|CometManifest")}};
    integration.registrationGroupKey = registration.groupKey;
    integration.outputFilename = QStringLiteral("stacked_comet_manifest.fit");
    integration.resolution = QStringLiteral("16x16");
    integration.object = QStringLiteral("CometManifest");
    integration.lightFiles = lights;
    integration.stacking.cometStackingMode = comet_stacking_mode;
    manifest.integrationGroups.push_back(integration);

    return manifest;
}

void configureManifestBuilderCometWorkflow(IntegrationDataModel& model, const QString& output_dir,
                                           const QString& suffix, const QString& stacking_mode)
{
    model.setOutputDirectory(output_dir);
    const FrameInfo first =
        makeFrame(FrameType::Light, QStringLiteral("manifest_builder_%1_a.fit").arg(suffix), QSize(100, 100), 60.0,
                  QStringLiteral("L"), QStringLiteral("ManifestBuilderComet"), QStringLiteral("2025-01-01"));
    const FrameInfo second =
        makeFrame(FrameType::Light, QStringLiteral("manifest_builder_%1_b.fit").arg(suffix), QSize(100, 100), 60.0,
                  QStringLiteral("L"), QStringLiteral("ManifestBuilderComet"), QStringLiteral("2025-01-01"));
    model.addFrames(FrameType::Light, {first, second});

    ASSERT_FALSE(model.registrationGroups().empty());
    const GroupKey registration_group_key =
        RegistrationGroupEngine::toMachineGroupKey(model.registrationGroups().begin()->first);
    RegistrationSettings registration_settings;
    registration_settings.registrationMethod = QStringLiteral("comet_tracking");
    registration_settings.referenceImage = first.id.canonicalPath();
    registration_settings.cometTracking = makeManifestCometSettings(std::vector<FrameId>{first.id, second.id});
    registration_settings.cometTracking->outputMode = stacking_mode;
    model.setRegistrationSettings(registration_group_key, registration_settings);

    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    StackingSettings stacking_settings;
    stacking_settings.cometStackingMode = QStringLiteral("Stars");
    model.setStackingSettings(FrameType::Light, integration_group_key, stacking_settings);
}

QString validationMessages(const WorkflowValidationResult& validation)
{
    QStringList messages;
    for (const auto& issue : validation.issues)
    {
        messages.push_back(issue.message);
    }
    return messages.join(QStringLiteral("\n"));
}

/// Validates a manifest reading every group's frame headers through an engine session, as the executor does.
WorkflowValidationResult validateReadingHeaders(const WorkflowManifest& manifest)
{
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession(1u);
    if (!session.hasValue())
    {
        ADD_FAILURE() << "Could not open a session: " << session.status().message();
        return manifest.validate();
    }
    return manifest.validate(ImageIntegration::sessionFrameHeaderReader(*session));
}

bool hasValidationIssue(const WorkflowValidationResult& validation, FixTarget target, const QString& needle)
{
    return std::find_if(validation.issues.begin(), validation.issues.end(), [&](const ValidationIssue& issue) {
               return issue.fixTarget == target && issue.message.contains(needle, Qt::CaseInsensitive);
           }) != validation.issues.end();
}

WorkflowManifest makeManifestWithFlatCalibrationGroup()
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("D:/Output");

    WorkflowManifest::CalibrationStage flat;
    flat.frameType = QStringLiteral("FLAT");

    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("flat|100x100|L")};
    group.outputFilename = QStringLiteral("master_flat.fit");
    group.inputFiles = {
        FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("flat_a.fit")),
        FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("flat_b.fit")),
    };
    group.biasMaster = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("master_bias.fit"));
    group.darkMaster = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("master_dark.fit"));

    flat.groups.push_back(std::move(group));
    manifest.flatStage = std::move(flat);
    return manifest;
}
} // namespace

TEST(ManifestBuilder, BuildsMinimalBiasStage)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.biasStage.has_value());
    ASSERT_EQ(manifest.biasStage->groups.size(), 1u);
    EXPECT_EQ(manifest.biasStage->frameType, QStringLiteral("BIAS"));
    EXPECT_EQ(manifest.biasStage->groups.front().inputFiles.size(), 1u);
    EXPECT_FALSE(manifest.biasStage->groups.front().outputFilename.isEmpty());
    EXPECT_EQ(manifest.biasStage->groups.front().stacking.normalizationMode, NormalizationMode::None);
}

TEST(ManifestBuilder, IntegrationGroupsStorePerLightExposureSeconds)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 90.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_EQ(group.lightFiles.size(), 2u);
    ASSERT_EQ(group.lightExposureSeconds.size(), group.lightFiles.size());

    std::vector<double> exposures = group.lightExposureSeconds;
    std::sort(exposures.begin(), exposures.end());
    ASSERT_EQ(exposures.size(), 2u);
    EXPECT_DOUBLE_EQ(exposures[0], 60.0);
    EXPECT_DOUBLE_EQ(exposures[1], 90.0);

    const WorkflowManifest roundTrip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(roundTrip.integrationGroups.size(), 1u);
    ASSERT_EQ(roundTrip.integrationGroups.front().lightExposureSeconds.size(), 2u);
}

TEST(ManifestBuilder, RegistrationGroupsStorePerLightCometTiming)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    const QDateTime captured_a = QDateTime::fromString(QStringLiteral("2026-03-13T00:00:00.000Z"), Qt::ISODateWithMs);
    const QDateTime captured_b = QDateTime::fromString(QStringLiteral("2026-03-13T00:01:30.000Z"), Qt::ISODateWithMs);
    const FrameInfo light_b = makeFrame(FrameType::Light, QStringLiteral("registration_timing_b.fit"), QSize(100, 100),
                                        90.0, QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2026-03-13"),
                                        0.0, 100, 10, QString(), captured_b);
    const FrameInfo light_a = makeFrame(FrameType::Light, QStringLiteral("registration_timing_a.fit"), QSize(100, 100),
                                        60.0, QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2026-03-13"),
                                        0.0, 100, 10, QString(), captured_a);
    model.addFrames(FrameType::Light, {light_b, light_a});

    const WorkflowManifest manifest = ManifestBuilder(model).build();

    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    const auto& group = manifest.registrationGroups.front();
    ASSERT_EQ(group.lightFiles.size(), 2u);
    ASSERT_EQ(group.lightExposureSeconds.size(), group.lightFiles.size());
    ASSERT_EQ(group.lightCaptureTimestampsUtc.size(), group.lightFiles.size());

    const auto indexFor = [&](const FrameId& frame_id) {
        const auto it = std::find(group.lightFiles.cbegin(), group.lightFiles.cend(), frame_id);
        return it == group.lightFiles.cend()
                   ? std::optional<size_t>()
                   : std::optional<size_t>(static_cast<size_t>(std::distance(group.lightFiles.cbegin(), it)));
    };
    const std::optional<size_t> index_a = indexFor(light_a.id);
    const std::optional<size_t> index_b = indexFor(light_b.id);
    ASSERT_TRUE(index_a.has_value());
    ASSERT_TRUE(index_b.has_value());
    EXPECT_DOUBLE_EQ(group.lightExposureSeconds[*index_a], 60.0);
    EXPECT_DOUBLE_EQ(group.lightExposureSeconds[*index_b], 90.0);
    EXPECT_EQ(group.lightCaptureTimestampsUtc[*index_a], captured_a.toUTC().toString(Qt::ISODateWithMs));
    EXPECT_EQ(group.lightCaptureTimestampsUtc[*index_b], captured_b.toUTC().toString(Qt::ISODateWithMs));

    const QJsonObject registration_json =
        manifest.toJson().value(QStringLiteral("registrationGroups")).toArray().at(0).toObject();
    EXPECT_TRUE(registration_json.contains(QStringLiteral("lightExposureSeconds")));
    EXPECT_TRUE(registration_json.contains(QStringLiteral("lightCaptureTimestampsUtc")));

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.registrationGroups.size(), 1u);
    EXPECT_EQ(round_trip.registrationGroups.front().lightExposureSeconds, group.lightExposureSeconds);
    EXPECT_EQ(round_trip.registrationGroups.front().lightCaptureTimestampsUtc, group.lightCaptureTimestampsUtc);
}

TEST(WorkflowManifest, PlateSolveSettingsRoundTrip)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("/tmp/output");
    manifest.plateSolve.defaultSolver = QStringLiteral("internal_gaia");
    manifest.plateSolve.astapPath = QStringLiteral("/opt/astap/astap");
    manifest.plateSolve.astrometryLocalPath = QStringLiteral("/usr/bin/solve-field");
    manifest.plateSolve.astrometryApiKey = QStringLiteral("test-api-key");
    manifest.plateSolve.indexPath = QStringLiteral("/data/astrometry");
    manifest.plateSolve.gaiaCatalogDirectory = QStringLiteral("/data/catalogs/gaia_dr3");
    manifest.plateSolve.gaiaCatalogPack = QStringLiteral("gaia_dr3_g14");
    manifest.plateSolve.searchRadius = 7.5;
    manifest.plateSolve.downsample = 3;
    manifest.plateSolve.timeoutSeconds = 240;
    manifest.plateSolve.useHints = false;
    manifest.plateSolve.focalLengthMm = 714.3;
    manifest.plateSolve.pixelSizeUm = 3.76;

    const QJsonObject json = manifest.toJson();
    const QByteArray serialized = QJsonDocument(json).toJson(QJsonDocument::Compact);
    EXPECT_FALSE(QString::fromUtf8(serialized).contains(QStringLiteral("test-api-key")));

    const QJsonObject plate_solve_json = json.value(QStringLiteral("plateSolve")).toObject();
    EXPECT_FALSE(plate_solve_json.contains(QStringLiteral("astrometryApiKey")));
    EXPECT_TRUE(plate_solve_json.value(QStringLiteral("astrometryApiKeyRedacted")).toBool());
    EXPECT_FALSE(plate_solve_json.value(QStringLiteral("astrometryApiKeyHash")).toString().isEmpty());

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(json);
    EXPECT_EQ(round_trip.plateSolve.defaultSolver, manifest.plateSolve.defaultSolver);
    EXPECT_EQ(round_trip.plateSolve.astapPath, manifest.plateSolve.astapPath);
    EXPECT_EQ(round_trip.plateSolve.astrometryLocalPath, manifest.plateSolve.astrometryLocalPath);
    EXPECT_TRUE(round_trip.plateSolve.astrometryApiKey.isEmpty());
    EXPECT_EQ(round_trip.astrometryApiKeyHash,
              plate_solve_json.value(QStringLiteral("astrometryApiKeyHash")).toString());
    EXPECT_EQ(round_trip.plateSolve.indexPath, manifest.plateSolve.indexPath);
    EXPECT_EQ(round_trip.plateSolve.gaiaCatalogDirectory, manifest.plateSolve.gaiaCatalogDirectory);
    EXPECT_EQ(round_trip.plateSolve.gaiaCatalogPack, manifest.plateSolve.gaiaCatalogPack);
    EXPECT_DOUBLE_EQ(round_trip.plateSolve.searchRadius, manifest.plateSolve.searchRadius);
    EXPECT_EQ(round_trip.plateSolve.downsample, manifest.plateSolve.downsample);
    EXPECT_EQ(round_trip.plateSolve.timeoutSeconds, manifest.plateSolve.timeoutSeconds);
    EXPECT_EQ(round_trip.plateSolve.useHints, manifest.plateSolve.useHints);
    ASSERT_TRUE(round_trip.plateSolve.focalLengthMm.has_value());
    ASSERT_TRUE(round_trip.plateSolve.pixelSizeUm.has_value());
    EXPECT_DOUBLE_EQ(*round_trip.plateSolve.focalLengthMm, *manifest.plateSolve.focalLengthMm);
    EXPECT_DOUBLE_EQ(*round_trip.plateSolve.pixelSizeUm, *manifest.plateSolve.pixelSizeUm);
}

// A manifest states its plate-solve key only by the key's hash. A key a manifest states in plain is never read - not as
// the key and not as a hash of it - and the manifest written again states neither.
TEST(WorkflowManifest, APlainApiKeyInAManifestIsNeverRead)
{
    QJsonObject plate_solve;
    plate_solve.insert(QStringLiteral("defaultSolver"), QStringLiteral("astrometry_net"));
    plate_solve.insert(QStringLiteral("astrometryApiKey"), QStringLiteral("plain-secret-api-key"));

    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);
    json.insert(QStringLiteral("outputDirectory"), QStringLiteral("/tmp/output"));
    json.insert(QStringLiteral("plateSolve"), plate_solve);

    const WorkflowManifest manifest = WorkflowManifest::fromJson(json);

    EXPECT_TRUE(manifest.plateSolve.astrometryApiKey.isEmpty());
    EXPECT_TRUE(manifest.astrometryApiKeyHash.isEmpty());

    const QByteArray serialized = QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact);
    EXPECT_FALSE(QString::fromUtf8(serialized).contains(QStringLiteral("plain-secret-api-key")));
    EXPECT_FALSE(
        manifest.toJson().value(QStringLiteral("plateSolve")).toObject().contains(QStringLiteral("astrometryApiKey")));
}

// A run is a full, an incremental or a registration-only one, and a manifest keeps the value it stores. The values 1, 3
// and 4 named calibration-only, normalization-only and stacking-only runs no version ever started; a manifest storing
// one - only a hand edit can - reads as a full run, as any value no mode names does.
TEST(WorkflowManifest, AStoredModeNoRunStartsReadsAsAFullRun)
{
    QJsonObject json;
    json.insert(QStringLiteral("version"), 1);
    json.insert(QStringLiteral("outputDirectory"), QStringLiteral("/tmp/output"));
    for (const int unnamed : {1, 3, 4, 6})
    {
        json.insert(QStringLiteral("executionMode"), unnamed);
        EXPECT_EQ(WorkflowManifest::fromJson(json).executionMode, ExecutionMode::Full) << unnamed;
    }
    for (const ExecutionMode mode : {ExecutionMode::Full, ExecutionMode::RegistrationOnly, ExecutionMode::Incremental})
    {
        WorkflowManifest manifest;
        manifest.outputDirectory = QStringLiteral("/tmp/output");
        manifest.executionMode = mode;
        EXPECT_EQ(WorkflowManifest::fromJson(manifest.toJson()).executionMode, mode) << static_cast<int>(mode);
    }
}

TEST(ManifestBuilder, IncludesOptionalPlateSolveScaleHintsFromSettings)
{
    ScopedGlobalSettingsContext context;
    an::ui::IGlobalSettingsProvider* settings = &context;

    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_hint.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ASSERT_TRUE(
        settings->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER), QStringLiteral("internal_gaia")));
    ASSERT_TRUE(settings->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_FOCAL_LENGTH_MM), QStringLiteral("840.5")));
    ASSERT_TRUE(settings->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_PIXEL_SIZE_UM), QStringLiteral("3.76")));

    ManifestBuilder builder(model);
    const WorkflowManifest manifest = builder.build();

    EXPECT_EQ(manifest.plateSolve.defaultSolver, QStringLiteral("internal_gaia"));
    ASSERT_TRUE(manifest.plateSolve.focalLengthMm.has_value());
    ASSERT_TRUE(manifest.plateSolve.pixelSizeUm.has_value());
    EXPECT_DOUBLE_EQ(*manifest.plateSolve.focalLengthMm, 840.5);
    EXPECT_DOUBLE_EQ(*manifest.plateSolve.pixelSizeUm, 3.76);
}

TEST(ManifestBuilder, StoresPlateSolveAstrometryApiKeyHashFromSettings)
{
    ScopedGlobalSettingsContext context;
    an::ui::IGlobalSettingsProvider* settings = &context;

    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_api_key.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ASSERT_TRUE(settings->upsert(QString::fromUtf8(SettingsKeys::TOOLS_ASTROMETRY_API_KEY),
                                 QStringLiteral("manifest-builder-api-key")));

    ManifestBuilder builder(model);
    const WorkflowManifest manifest = builder.build();

    EXPECT_TRUE(manifest.plateSolve.astrometryApiKey.isEmpty());
    EXPECT_FALSE(manifest.astrometryApiKeyHash.isEmpty());

    const QJsonObject json = manifest.toJson();
    const QByteArray serialized = QJsonDocument(json).toJson(QJsonDocument::Compact);
    EXPECT_FALSE(QString::fromUtf8(serialized).contains(QStringLiteral("manifest-builder-api-key")));

    const QJsonObject plate_solve_json = json.value(QStringLiteral("plateSolve")).toObject();
    EXPECT_TRUE(plate_solve_json.value(QStringLiteral("astrometryApiKeyRedacted")).toBool());
    EXPECT_EQ(plate_solve_json.value(QStringLiteral("astrometryApiKeyHash")).toString(), manifest.astrometryApiKeyHash);
    EXPECT_FALSE(plate_solve_json.contains(QStringLiteral("astrometryApiKey")));
}

TEST(WorkflowManifest, PureCometTrackingAndStackingValidationIsEnabledForFullWorkflow)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const WorkflowManifest manifest = makePureCometManifest(temp_dir.path());

    const WorkflowValidationResult validation = validateReadingHeaders(manifest);
    EXPECT_FALSE(validation.hasErrors()) << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, StarsPlusCometExperimentalValidationAcceptsExplicitOptIn)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString token = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
    const WorkflowManifest manifest = makePureCometManifest(temp_dir.path(), token);

    const WorkflowValidationResult validation = validateReadingHeaders(manifest);
    EXPECT_FALSE(validation.hasErrors()) << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, AnUnofferedStarsPlusCometStringRemainsUnknownAndDoesNotSatisfyCometTracking)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString unoffered_mode = QStringLiteral("Stars+Comet");
    WorkflowManifest manifest = makePureCometManifest(temp_dir.path(), unoffered_mode);
    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());

    ASSERT_FALSE(round_trip.integrationGroups.empty());
    EXPECT_EQ(round_trip.integrationGroups.front().stacking.cometStackingMode, unoffered_mode);
    EXPECT_FALSE(ImageIntegration::usesCometStackingMode(unoffered_mode));

    const WorkflowValidationResult validation = round_trip.validate();
    EXPECT_TRUE(validation.hasErrors());
    EXPECT_TRUE(hasValidationIssue(validation, FixTarget::IntegrationTab,
                                   QStringLiteral("Comet tracking registration requires Comet stacking")))
        << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, CometStackingRequiresCometTrackingRegistrationGroup)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makePureCometManifest(temp_dir.path(), QStringLiteral("Comet"));
    auto& registration = manifest.registrationGroups.front();
    registration.settings.registrationMethod = QStringLiteral("star_matching");
    registration.settings.cometTracking.reset();

    const WorkflowValidationResult validation = manifest.validate();

    EXPECT_TRUE(validation.hasErrors());
    EXPECT_TRUE(hasValidationIssue(validation, FixTarget::IntegrationTab,
                                   QStringLiteral("Comet stacking requires a comet tracking registration group")))
        << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, CometTrackingRequiresExplicitReferenceFrame)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makePureCometManifest(temp_dir.path(), QStringLiteral("Comet"));
    manifest.registrationGroups.front().referenceFrame.reset();

    const WorkflowValidationResult validation = manifest.validate();

    EXPECT_TRUE(validation.hasErrors());
    EXPECT_TRUE(hasValidationIssue(validation, FixTarget::RegistrationTab,
                                   QStringLiteral("Comet tracking requires an explicit reference frame")))
        << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, StarsStackingModeDoesNotTriggerCometStackingGate)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QDir::tempPath();

    const FrameId light(QStringLiteral("/tmp/stars_stacking_light.fit"));

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|StarsStacking")};
    registration.resolution = QStringLiteral("100x100");
    registration.object = QStringLiteral("StarsStacking");
    registration.lightFiles = {light};
    registration.referenceFrame = light;
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|100x100|StarsStacking")}};
    integration.registrationGroupKey = registration.groupKey;
    integration.outputFilename = QStringLiteral("stacked_stars.fit");
    integration.resolution = QStringLiteral("100x100");
    integration.object = QStringLiteral("StarsStacking");
    integration.lightFiles = {light};
    integration.stacking.cometStackingMode = QStringLiteral("Stars");
    manifest.integrationGroups.push_back(integration);

    const WorkflowValidationResult validation = manifest.validate();
    const auto it = std::find_if(validation.issues.begin(), validation.issues.end(), [](const ValidationIssue& issue) {
        return issue.message.contains(QStringLiteral("temporarily disabled"), Qt::CaseInsensitive);
    });
    EXPECT_EQ(it, validation.issues.end());
}

TEST(ManifestBuilder, IntegrationOutputFilenamesAreDisambiguatedWhenSanitizedNamesCollide)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_obj_slash.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M/83"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Light, QStringLiteral("light_obj_pipe.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M|83"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 2u);

    const QString filenameA = manifest.integrationGroups[0].outputFilename;
    const QString filenameB = manifest.integrationGroups[1].outputFilename;

    EXPECT_FALSE(filenameA.isEmpty());
    EXPECT_FALSE(filenameB.isEmpty());
    EXPECT_NE(filenameA, filenameB);
}

TEST(ManifestBuilder, BuildsMinimalDarkStage)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 1u);
    EXPECT_EQ(manifest.darkStage->frameType, QStringLiteral("DARK"));
    EXPECT_EQ(manifest.darkStage->groups.front().inputFiles.size(), 1u);
    EXPECT_FALSE(manifest.darkStage->groups.front().outputFilename.isEmpty());
    EXPECT_EQ(manifest.darkStage->groups.front().stacking.normalizationMode, NormalizationMode::None);
}

// A master dark is stacked from its frames as they are, holding their bias, in CCD mode as in standard mode, so a dark
// group names no bias: CCD mode subtracts the bias from the master once, where a light is calibrated. The dark stage
// took the bias from every dark frame in CCD mode, and the light's calibration took it from that master again.
TEST(ManifestBuilder, CcdModeNamesNoBiasForADarkGroup)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());

    auto policy = model.calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model.setCalibrationMatchingPolicy(policy);

    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01")),
                                      makeFrame(FrameType::Dark, QStringLiteral("dark2.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_TRUE(manifest.biasStage.has_value());
    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 1u);
    EXPECT_FALSE(manifest.darkStage->groups.front().biasMaster.has_value());

    // The light takes the bias and the dark, and subtracts the bias from the dark itself.
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    EXPECT_TRUE(manifest.lightsCalibration.front().biasPath.has_value());
    EXPECT_TRUE(manifest.lightsCalibration.front().darkPath.has_value());
    EXPECT_TRUE(manifest.lightsCalibration.front().settings.subtractBiasFromDark);
}

TEST(ManifestBuilder, BuildsMinimalFlatStage)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0, QStringLiteral("L"),
                               QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    EXPECT_EQ(manifest.flatStage->frameType, QStringLiteral("FLAT"));
    EXPECT_EQ(manifest.flatStage->groups.front().inputFiles.size(), 1u);
    EXPECT_FALSE(manifest.flatStage->groups.front().outputFilename.isEmpty());
    EXPECT_EQ(manifest.flatStage->groups.front().stacking.normalizationMode, NormalizationMode::Global);
    EXPECT_EQ(manifest.flatStage->groups.front().stacking.globalNormalizationMethod,
              GlobalNormalizationMethod::Multiplicative);
}

TEST(ManifestBuilder, FlatStageIncludesBiasAndDarkCalibration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add bias, dark, and flat frames with matching resolution
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"), QSize(100, 100), 1.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat1.fit"), QSize(100, 100), 1.0, QStringLiteral("L"),
                               QStringLiteral("M42"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Flat, QStringLiteral("flat2.fit"), QSize(100, 100), 1.0, QStringLiteral("L"),
                               QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);

    auto& flat_group = manifest.flatStage->groups.front();
    EXPECT_EQ(flat_group.inputFiles.size(), 2u);
    EXPECT_FALSE(flat_group.skipIfExists);

    // Standard mode: the dark holds the bias, so a flat takes the dark alone, as a light does.
    EXPECT_FALSE(flat_group.biasMaster.has_value()) << "the flat's dark already holds the bias";
    EXPECT_TRUE(flat_group.darkMaster.has_value()) << "Flat group should have dark master assigned";

    // CCD mode: a master dark holds its bias there too, so the flat takes the dark alone - taking the bias from the
    // flat and from the dark leaves the same flat at its factor of one.
    CalibrationMatchingPolicy ccd = model.calibrationMatchingPolicy();
    ccd.subtractBiasFromDark = true;
    model.setCalibrationMatchingPolicy(ccd);
    const WorkflowManifest ccd_manifest = ManifestBuilder(model).build();
    ASSERT_TRUE(ccd_manifest.flatStage.has_value());
    ASSERT_EQ(ccd_manifest.flatStage->groups.size(), 1u);
    EXPECT_FALSE(ccd_manifest.flatStage->groups.front().biasMaster.has_value());
    EXPECT_TRUE(ccd_manifest.flatStage->groups.front().darkMaster.has_value());
}

TEST(ManifestBuilder, FlatStageTakesTheBiasWhenNoDarkMatches)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias_only.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat_bias_only.fit"), QSize(100, 100), 1.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_bias_only.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
    EXPECT_TRUE(manifest.flatStage->groups.front().biasMaster.has_value());
    EXPECT_FALSE(manifest.flatStage->groups.front().darkMaster.has_value());
}

// A flat's dark is matched as a light's of the flat's exposure and temperature is, as the Flat panel matches it: among
// darks at the flats' exposure, the one nearest the flats' temperature is stacked and named, not the first by group
// key.
TEST(ManifestBuilder, AFlatTakesTheDarkNearestItsTemperatureAsTheFlatPanelDoes)
{
    for (const double flatTemperature : {-10.0, -20.0})
    {
        SCOPED_TRACE(flatTemperature);
        QTemporaryDir temp_dir;
        ASSERT_TRUE(temp_dir.isValid());
        IntegrationDataModel model;
        model.setOutputDirectory(temp_dir.path());
        GroupingOptions darkGrouping = model.groupingOptions(FrameType::Dark);
        darkGrouping.byTemperature = true;
        model.setGroupingOptions(FrameType::Dark, darkGrouping);
        model.addFrames(FrameType::Dark,
                        {makeFrame(FrameType::Dark, QStringLiteral("flat_dark_m10.fit"), QSize(100, 100), 1.0,
                                   QString(), QString(), QStringLiteral("2025-01-01"), -10.0),
                         makeFrame(FrameType::Dark, QStringLiteral("flat_dark_m20.fit"), QSize(100, 100), 1.0,
                                   QString(), QString(), QStringLiteral("2025-01-01"), -20.0)});
        model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat_tempered.fit"),
                                                    QSize(100, 100), 1.0, QStringLiteral("L"), QStringLiteral("M42"),
                                                    QStringLiteral("2025-01-01"), flatTemperature)});
        model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light_tempered.fit"),
                                                     QSize(100, 100), 60.0, QStringLiteral("L"), QStringLiteral("M42"),
                                                     QStringLiteral("2025-01-01"), flatTemperature)});
        ASSERT_EQ(model.darkGroups().size(), 2u);

        const WorkflowManifest manifest = ManifestBuilder(model).build();
        ASSERT_TRUE(manifest.flatStage.has_value());
        ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
        const auto& flat = manifest.flatStage->groups.front();
        ASSERT_TRUE(flat.darkMaster.has_value());
        ASSERT_TRUE(manifest.darkStage.has_value());

        const QString expectedDark =
            flatTemperature == -10.0 ? QStringLiteral("flat_dark_m10.fit") : QStringLiteral("flat_dark_m20.fit");
        const auto stacked =
            std::find_if(manifest.darkStage->groups.begin(), manifest.darkStage->groups.end(), [&](const auto& group) {
                return std::any_of(group.inputFiles.begin(), group.inputFiles.end(), [&](const FrameId& file) {
                    return QFileInfo(file.canonicalPath()).fileName() == expectedDark;
                });
            });
        ASSERT_NE(stacked, manifest.darkStage->groups.end()) << "the flat's dark is stacked";
        EXPECT_EQ(flat.darkMaster->canonicalPath(),
                  FrameId(QDir(temp_dir.path()).filePath(stacked->outputFilename)).canonicalPath());
    }
}

// A dark a light is matched to by the exposure fallback is stacked and named for that light, as the Light panel shows
// it. The run stacked only the darks whose exposure a light shares, and calibrated such a light with its bias alone.
TEST(ManifestBuilder, ADarkALightIsMatchedToByTheExposureFallbackIsStackedAndNamed)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias_fallback.fit"), QSize(100, 100),
                                                0.0, QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark_fallback.fit"), QSize(100, 100),
                                                55.0, QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_fallback.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    ASSERT_TRUE(model.calibrationMatchingPolicy().allowDarkExposureFallback);

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_TRUE(manifest.darkStage.has_value());
    ASSERT_EQ(manifest.darkStage->groups.size(), 1u);
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    const auto& light = manifest.lightsCalibration.front();
    ASSERT_TRUE(light.darkPath.has_value());
    EXPECT_EQ(
        light.darkPath->canonicalPath(),
        FrameId(QDir(temp_dir.path()).filePath(manifest.darkStage->groups.front().outputFilename)).canonicalPath());
    EXPECT_FALSE(light.biasPath.has_value()) << "standard mode: the dark holds the bias";
}

// A flat whose dark the person unassigned in the Flat panel takes no dark, so it takes the bias, as a flat no dark
// matches does, in CCD mode too: a master flat with neither keeps the bias pedestal, and every light it flat-fields is
// wrong. The bias group is stacked for the flat, although no light needs it in standard mode.
TEST(ManifestBuilder, AFlatWhoseDarkThePersonUnassignedTakesTheBias)
{
    for (const bool ccdMode : {false, true})
    {
        SCOPED_TRACE(ccdMode ? "CCD mode" : "standard mode");
        QTemporaryDir temp_dir;
        ASSERT_TRUE(temp_dir.isValid());
        IntegrationDataModel model;
        model.setOutputDirectory(temp_dir.path());
        CalibrationMatchingPolicy policy = model.calibrationMatchingPolicy();
        policy.subtractBiasFromDark = ccdMode;
        model.setCalibrationMatchingPolicy(policy);
        model.addFrames(FrameType::Bias,
                        {makeFrame(FrameType::Bias, QStringLiteral("bias_unassigned_dark.fit"), QSize(100, 100), 0.0,
                                   QString(), QString(), QStringLiteral("2025-01-01"))});
        model.addFrames(FrameType::Dark,
                        {makeFrame(FrameType::Dark, QStringLiteral("flat_dark_unassigned.fit"), QSize(100, 100), 2.0,
                                   QString(), QString(), QStringLiteral("2025-01-01")),
                         makeFrame(FrameType::Dark, QStringLiteral("light_dark_unassigned.fit"), QSize(100, 100), 300.0,
                                   QString(), QString(), QStringLiteral("2025-01-01"))});
        model.addFrames(FrameType::Flat,
                        {makeFrame(FrameType::Flat, QStringLiteral("flat_unassigned_dark.fit"), QSize(100, 100), 2.0,
                                   QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
        model.addFrames(FrameType::Light,
                        {makeFrame(FrameType::Light, QStringLiteral("light_unassigned_dark.fit"), QSize(100, 100),
                                   300.0, QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
        ASSERT_EQ(model.flatGroups().size(), 1u);
        ASSERT_EQ(model.darkGroups().size(), 2u);
        model.setCalibrationOverride(LightGroupKey::fromFrame(model.flatGroups().front().frames.front()),
                                     CalibrationOverrideType::Dark, QString::fromLatin1(kUnassignedCalibrationMarker));

        const WorkflowManifest manifest = ManifestBuilder(model).build();
        ASSERT_TRUE(manifest.flatStage.has_value());
        ASSERT_EQ(manifest.flatStage->groups.size(), 1u);
        const auto& flat = manifest.flatStage->groups.front();
        EXPECT_FALSE(flat.darkMaster.has_value()) << "the person unassigned the flat's dark";
        ASSERT_TRUE(flat.biasMaster.has_value()) << "a flat no dark is applied to takes the bias";
        ASSERT_TRUE(manifest.biasStage.has_value()) << "the bias the flat takes is stacked";
        ASSERT_EQ(manifest.biasStage->groups.size(), 1u);
        EXPECT_EQ(
            flat.biasMaster->canonicalPath(),
            FrameId(QDir(temp_dir.path()).filePath(manifest.biasStage->groups.front().outputFilename)).canonicalPath());
    }
}

// A light whose dark the person unassigned takes the bias in standard mode, as a light no dark matches does: the light
// is otherwise flat-fielded with its bias pedestal in it.
TEST(ManifestBuilder, ALightWhoseDarkThePersonUnassignedTakesTheBias)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());
    ASSERT_FALSE(model.calibrationMatchingPolicy().subtractBiasFromDark);
    model.addFrames(FrameType::Bias,
                    {makeFrame(FrameType::Bias, QStringLiteral("bias_light_unassigned.fit"), QSize(100, 100), 0.0,
                               QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark,
                    {makeFrame(FrameType::Dark, QStringLiteral("dark_light_unassigned.fit"), QSize(100, 100), 60.0,
                               QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_light_unassigned.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.setCalibrationOverride(LightGroupKey::fromFrame(model.lightGroups().front().frames.front()),
                                 CalibrationOverrideType::Dark, QString::fromLatin1(kUnassignedCalibrationMarker));

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    const auto& light = manifest.lightsCalibration.front();
    EXPECT_FALSE(light.darkPath.has_value()) << "the person unassigned the light's dark";
    ASSERT_TRUE(light.biasPath.has_value()) << "a light no dark is applied to takes the bias";
    ASSERT_TRUE(manifest.biasStage.has_value());
    ASSERT_EQ(manifest.biasStage->groups.size(), 1u);
    EXPECT_EQ(
        light.biasPath->canonicalPath(),
        FrameId(QDir(temp_dir.path()).filePath(manifest.biasStage->groups.front().outputFilename)).canonicalPath());
}

// A calibration group the person assigns by hand from the Light panel's candidates - any group of the light's
// resolution, not only one that would match - is stacked for the light and named as its master, as a group the light
// matches is: the override holds the master the panel names for the group, which the run resolves to the group. None
// of these groups matches the light: the bias is of another gain, the dark of another exposure with the exposure
// fallback off, the flat of another filter.
TEST(ManifestBuilder, ACalibrationGroupAssignedByHandIsStackedForTheLight)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());
    CalibrationMatchingPolicy policy = model.calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    policy.allowDarkExposureFallback = false;
    model.setCalibrationMatchingPolicy(policy);
    GroupingOptions biasGrouping = model.groupingOptions(FrameType::Bias);
    biasGrouping.byIsoGain = true;
    model.setGroupingOptions(FrameType::Bias, biasGrouping);
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias_by_hand.fit"), QSize(100, 100),
                                                0.0, QString(), QString(), QStringLiteral("2025-01-01"), 0.0, 200)});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark_by_hand.fit"), QSize(100, 100),
                                                120.0, QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat_by_hand.fit"), QSize(100, 100), 2.0,
                               QStringLiteral("Ha"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_by_hand.fit"), QSize(100, 100), 300.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    ASSERT_EQ(model.biasGroups().size(), 1u);
    ASSERT_EQ(model.darkGroups().size(), 1u);
    ASSERT_EQ(model.flatGroups().size(), 1u);
    const LightGroupKey lightKey = LightGroupKey::fromFrame(model.lightGroups().front().frames.front());

    {
        const WorkflowManifest unassigned = ManifestBuilder(model).build();
        ASSERT_EQ(unassigned.lightsCalibration.size(), 1u);
        ASSERT_FALSE(unassigned.lightsCalibration.front().biasPath.has_value()) << "no group matches the light";
        ASSERT_FALSE(unassigned.lightsCalibration.front().darkPath.has_value()) << "no group matches the light";
        ASSERT_FALSE(unassigned.lightsCalibration.front().flatPath.has_value()) << "no group matches the light";
    }

    model.setCalibrationOverride(
        lightKey, CalibrationOverrideType::Bias,
        canonicalMasterPathForGroup(&model, CalibrationOverrideType::Bias, model.biasGroups().front()));
    model.setCalibrationOverride(
        lightKey, CalibrationOverrideType::Dark,
        canonicalMasterPathForGroup(&model, CalibrationOverrideType::Dark, model.darkGroups().front()));
    model.setCalibrationOverride(
        lightKey, CalibrationOverrideType::Flat,
        canonicalMasterPathForGroup(&model, CalibrationOverrideType::Flat, model.flatGroups().front()));

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    const auto& light = manifest.lightsCalibration.front();
    const auto masterOf = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage,
                              const GroupKey& key) -> std::optional<QString> {
        if (!stage.has_value())
        {
            return std::nullopt;
        }
        for (const auto& group : stage->groups)
        {
            if (group.groupKey == key)
            {
                return FrameId(QDir(temp_dir.path()).filePath(group.outputFilename)).canonicalPath();
            }
        }
        return std::nullopt;
    };

    const std::optional<QString> bias = masterOf(manifest.biasStage, model.biasGroups().front().key);
    ASSERT_TRUE(bias.has_value()) << "the bias group assigned by hand is stacked";
    ASSERT_TRUE(light.biasPath.has_value());
    EXPECT_EQ(light.biasPath->canonicalPath(), *bias);

    const std::optional<QString> dark = masterOf(manifest.darkStage, model.darkGroups().front().key);
    ASSERT_TRUE(dark.has_value()) << "the dark group assigned by hand is stacked";
    ASSERT_TRUE(light.darkPath.has_value());
    EXPECT_EQ(light.darkPath->canonicalPath(), *dark);

    const std::optional<QString> flat = masterOf(manifest.flatStage, model.flatGroups().front().key);
    ASSERT_TRUE(flat.has_value()) << "the flat group assigned by hand is stacked";
    ASSERT_TRUE(light.flatPath.has_value());
    EXPECT_EQ(light.flatPath->canonicalPath(), *flat);
}

// A bias or dark master is combined without normalization. The manifest keeps a normalization such a group still
// states, so the master's stack can leave it out and say so rather than drop it unseen - beside the settings the master
// is made with, so the master is known by what it is: a stated choice that changes nothing re-stacks nothing.
TEST(ManifestBuilder, BiasAndDarkGroupsKeepTheNormalizationTheyStateForTheStackToLeaveOut)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias_normalized.fit"), QSize(100, 100),
                                                0.0, QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_normalized.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    ASSERT_EQ(model.biasGroups().size(), 1u);
    StackingSettings stated = defaultCalibrationStackingSettings(FrameType::Bias);
    stated.normalizationMode = NormalizationMode::Global;
    stated.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
    model.setStackingSettings(FrameType::Bias, model.biasGroups().front().key, stated);

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_TRUE(manifest.biasStage.has_value());
    ASSERT_EQ(manifest.biasStage->groups.size(), 1u);
    const auto& group = manifest.biasStage->groups.front();
    EXPECT_EQ(group.statedNormalization, NormalizationMode::Global);
    EXPECT_EQ(group.stacking.normalizationMode, NormalizationMode::None);
    EXPECT_FALSE(group.stacking.localNormalization.enabled);

    const WorkflowManifest roundTripped = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_TRUE(roundTripped.biasStage.has_value());
    ASSERT_EQ(roundTripped.biasStage->groups.size(), 1u);
    EXPECT_EQ(roundTripped.biasStage->groups.front().statedNormalization, NormalizationMode::Global);

    // The dialog now fixes a bias group's choice at None: storing it so changes nothing the master is made with.
    StackingSettings fixed = stated;
    fixed.normalizationMode = NormalizationMode::None;
    model.setStackingSettings(FrameType::Bias, model.biasGroups().front().key, fixed);
    const WorkflowManifest refixed = ManifestBuilder(model).build();
    ASSERT_TRUE(refixed.biasStage.has_value());
    ASSERT_EQ(refixed.biasStage->groups.size(), 1u);
    EXPECT_EQ(refixed.biasStage->groups.front().statedNormalization, NormalizationMode::None);
    EXPECT_EQ(computeCalibrationGroupHash(refixed, refixed.biasStage->groups.front()),
              computeCalibrationGroupHash(manifest, group))
        << "a normalization the master is combined without is not part of what the master is";
}

TEST(ManifestBuilder, FlatStageOmitsCalibrationForExistingMaster)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add bias and dark frames
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias_for_flat.fit"), QSize(100, 100),
                                                0.0, QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark_for_flat.fit"), QSize(100, 100),
                                                1.0, QString(), QString(), QStringLiteral("2025-01-01"))});

    // Add a flat frame and mark it as a master
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("master_flat.fit"), QSize(100, 100), 1.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.setMasterStatus(QDir::tempPath() + QDir::separator() + QStringLiteral("master_flat.fit"), true,
                          MasterSource::UserDefined);

    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_for_flat.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.flatStage->groups.size(), 1u);

    auto& flat_group = manifest.flatStage->groups.front();
    // Existing master should be used, so calibration masters are not needed
    EXPECT_TRUE(flat_group.skipIfExists);
    EXPECT_FALSE(flat_group.biasMaster.has_value()) << "Existing master should not have bias assigned";
    EXPECT_FALSE(flat_group.darkMaster.has_value()) << "Existing master should not have dark assigned";
}

TEST(ManifestBuilder, FlatStageKeepsCalibrationForGeneratedExistingOutput)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());

    model.addFrames(FrameType::Bias,
                    {makeFrame(FrameType::Bias, QStringLiteral("bias_generated_flat.fit"), QSize(100, 100), 0.0,
                               QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark,
                    {makeFrame(FrameType::Dark, QStringLiteral("dark_generated_flat.fit"), QSize(100, 100), 1.0,
                               QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat_generated_1.fit"), QSize(100, 100), 1.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Flat, QStringLiteral("flat_generated_2.fit"), QSize(100, 100), 1.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_generated_flat.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest initial = builder.build();
    ASSERT_TRUE(initial.flatStage.has_value());
    ASSERT_EQ(initial.flatStage->groups.size(), 1u);
    ASSERT_FALSE(initial.flatStage->groups.front().biasMaster.has_value()) << "the flat's dark holds the bias";
    ASSERT_TRUE(initial.flatStage->groups.front().darkMaster.has_value());

    const QString output_path = QDir(temp_dir.path()).filePath(initial.flatStage->groups.front().outputFilename);
    QFile file(output_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("flat");
    file.close();

    WorkflowManifest incremental = builder.build();
    ASSERT_TRUE(incremental.flatStage.has_value());
    ASSERT_EQ(incremental.flatStage->groups.size(), 1u);

    const auto& flat_group = incremental.flatStage->groups.front();
    EXPECT_TRUE(flat_group.skipIfExists);
    EXPECT_FALSE(flat_group.existingMaster.has_value());
    EXPECT_FALSE(flat_group.biasMaster.has_value());
    ASSERT_TRUE(flat_group.darkMaster.has_value());
    EXPECT_EQ(flat_group.darkMaster->canonicalPath(), initial.flatStage->groups.front().darkMaster->canonicalPath());
}

TEST(ManifestBuilder, BuildsMinimalLightsStage)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    EXPECT_FALSE(manifest.biasStage.has_value());
    EXPECT_FALSE(manifest.darkStage.has_value());
    EXPECT_FALSE(manifest.flatStage.has_value());
    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsRegistration")));
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsNormalization")));
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsStacking")));
}

TEST(ManifestBuilder, StackExclusionsPersistAndApplyOnlyToIntegrationGroups)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    const FrameInfo keep = makeFrame(FrameType::Light, QStringLiteral("stack_keep.fit"), QSize(100, 100), 60.0,
                                     QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    const FrameInfo drop = makeFrame(FrameType::Light, QStringLiteral("stack_drop.fit"), QSize(100, 100), 60.0,
                                     QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    model.addFrames(FrameType::Light, {keep, drop});

    ASSERT_EQ(model.integrationGroups().size(), 1u);
    const GroupKey integration_group_key = model.integrationGroups().front();
    model.setFramesStackExcluded(integration_group_key, {drop.id.canonicalPath()}, true);
    EXPECT_TRUE(model.isFrameStackExcluded(integration_group_key, drop.id));
    ASSERT_TRUE(model.lightFrames().findById(drop.id).has_value());
    EXPECT_FALSE(model.lightFrames().findById(drop.id)->excluded);

    IntegrationDataModel restored;
    restored.fromJson(model.toJson());
    ASSERT_EQ(restored.integrationGroups().size(), 1u);
    const GroupKey restored_group_key = restored.integrationGroups().front();
    EXPECT_TRUE(restored.isFrameStackExcluded(restored_group_key, drop.id));
    ASSERT_TRUE(restored.lightFrames().findById(drop.id).has_value());
    EXPECT_FALSE(restored.lightFrames().findById(drop.id)->excluded);

    const WorkflowManifest manifest = ManifestBuilder(restored).build();

    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    EXPECT_EQ(manifest.registrationGroups.front().lightFiles.size(), 2u);
    EXPECT_NE(std::find(manifest.registrationGroups.front().lightFiles.begin(),
                        manifest.registrationGroups.front().lightFiles.end(), keep.id),
              manifest.registrationGroups.front().lightFiles.end());
    EXPECT_NE(std::find(manifest.registrationGroups.front().lightFiles.begin(),
                        manifest.registrationGroups.front().lightFiles.end(), drop.id),
              manifest.registrationGroups.front().lightFiles.end());

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_EQ(manifest.integrationGroups.front().lightFiles.size(), 1u);
    EXPECT_NE(std::find(manifest.integrationGroups.front().lightFiles.begin(),
                        manifest.integrationGroups.front().lightFiles.end(), keep.id),
              manifest.integrationGroups.front().lightFiles.end());
    EXPECT_EQ(std::find(manifest.integrationGroups.front().lightFiles.begin(),
                        manifest.integrationGroups.front().lightFiles.end(), drop.id),
              manifest.integrationGroups.front().lightFiles.end());
}

TEST(ManifestBuilder, DerivesStarsStackingModeFromNonCometRegistrationSettings)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("derived_stars_mode_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("DerivedStars"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("derived_stars_mode_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("DerivedStars"), QStringLiteral("2025-01-01"));
    model.addFrames(FrameType::Light, {a, b});

    ASSERT_FALSE(model.integrationGroups().empty());
    const GroupKey integration_group_key = model.integrationGroups().front();
    StackingSettings stale_stacking_settings;
    stale_stacking_settings.cometStackingMode = QStringLiteral("Comet");
    model.setStackingSettings(FrameType::Light, integration_group_key, stale_stacking_settings);

    ManifestBuilder builder(model);
    const WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_EQ(manifest.integrationGroups.front().stacking.cometStackingMode, QStringLiteral("Stars"));

    const WorkflowValidationResult validation = manifest.validate();
    EXPECT_FALSE(hasValidationIssue(validation, FixTarget::IntegrationTab,
                                    QStringLiteral("Comet stacking requires a comet tracking registration group")))
        << validationMessages(validation).toStdString();
}

TEST(ManifestBuilder, RegistrationCometOutputModePreservesExactExperimentalTokenForSharedStackingGroups)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    configureManifestBuilderCometWorkflow(model, temp_dir.path(), QStringLiteral("builder_experimental"),
                                          ImageIntegration::starsPlusCometExperimentalStackingModeToken());

    ManifestBuilder builder(model);
    const WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_EQ(manifest.integrationGroups.front().stacking.cometStackingMode,
              ImageIntegration::starsPlusCometExperimentalStackingModeToken());
}

TEST(ManifestBuilder, IntegrationOutputFilenameUsesDisplayLabel)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    GroupingOptions light_options;
    light_options.byResolution = true;
    light_options.byFilter = true;
    light_options.byObject = true;
    light_options.bySession = false;
    model.setGroupingOptions(FrameType::Light, light_options);

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(3008, 3004), 60.0,
                                                 QStringLiteral("blue"), QStringLiteral("M 83"), QString())});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_EQ(manifest.integrationGroups.front().outputFilename, QStringLiteral("stacked_3008x3004_blue_M_83.fit"));
}

TEST(ManifestBuilder, UsesLightIntegrationGroupingOptions)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    auto calibration_options = model.groupingOptions(FrameType::Light);
    calibration_options.bySession = true;
    model.setGroupingOptions(FrameType::Light, calibration_options);

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 60.0,
                                                 QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("S1")),
                                       makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 60.0,
                                                 QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("S2"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);

    auto integration_options = model.lightIntegrationGroupingOptions();
    integration_options.bySession = true;
    model.setLightIntegrationGroupingOptions(integration_options);

    WorkflowManifest manifest_split = builder.build();
    EXPECT_EQ(manifest_split.integrationGroups.size(), 2u);
}

TEST(ManifestBuilder, IncludesOnlyRelevantGroups)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    GroupingOptions bias_options;
    bias_options.byIsoGain = true;
    model.setGroupingOptions(FrameType::Bias, bias_options);

    FrameInfo bias_100 = makeFrame(FrameType::Bias, QStringLiteral("bias_100.fit"), QSize(100, 100), 0.0, QString(),
                                   QString(), QStringLiteral("2025-01-01"), 0.0, 100, 10);
    FrameInfo bias_200 = makeFrame(FrameType::Bias, QStringLiteral("bias_200.fit"), QSize(100, 100), 0.0, QString(),
                                   QString(), QStringLiteral("2025-01-01"), 0.0, 200, 10);
    model.addFrames(FrameType::Bias, {bias_100, bias_200});

    model.addFrames(FrameType::Light, {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                                                 QStringLiteral("L"), QStringLiteral("M42"),
                                                 QStringLiteral("2025-01-01"), 0.0, 100, 10)});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_TRUE(manifest.biasStage.has_value());
    ASSERT_EQ(manifest.biasStage->groups.size(), 1u);

    GroupingEngine engine;
    GroupKey expected = engine.computeGroupKey(bias_100, FrameType::Bias, bias_options);
    EXPECT_EQ(manifest.biasStage->groups.front().groupKey, expected);
}

TEST(ManifestBuilder, ExcludesAlreadyProcessedGroups)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest initial = builder.build();
    ASSERT_EQ(initial.integrationGroups.size(), 1u);

    const QString output_path = QDir(temp_dir.path()).filePath(initial.integrationGroups.front().outputFilename);
    QFile file(output_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("test");
    file.close();

    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    EXPECT_TRUE(manifest.integrationGroups.front().skipIfExists);
}

TEST(ManifestBuilder, RespectsDependencyOrder)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    // Use Disk mode to ensure LightsCalibration stage is included for this order test
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);

    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat,
                    {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0, QStringLiteral("L"),
                               QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    const QStringList expected = {QStringLiteral("Bias"),
                                  QStringLiteral("Dark"),
                                  QStringLiteral("Flat"),
                                  QStringLiteral("LightsCalibration"),
                                  QStringLiteral("LightsRegistration"),
                                  QStringLiteral("LightsAlignment"),
                                  QStringLiteral("LightsNormalization"),
                                  QStringLiteral("LightsStacking")};
    EXPECT_EQ(manifest.stageSequence, expected);
}

TEST(ManifestBuilder, DeterministicOutput)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest first = builder.build();
    WorkflowManifest second = builder.build();

    const auto first_json = QJsonDocument(first.toJson()).toJson(QJsonDocument::Compact);
    const auto second_json = QJsonDocument(second.toJson()).toJson(QJsonDocument::Compact);

    EXPECT_EQ(first_json, second_json);
}

TEST(ManifestBuilder, NormalizationReferenceUsesBestQuality)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 60.0, QStringLiteral("L"),
                            QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 60.0, QStringLiteral("L"),
                            QStringLiteral("M42"), QStringLiteral("2025-01-01"));

    model.addFrames(FrameType::Light, {a, b});

    FrameQualityMetrics ma;
    ma.stars = 500;
    ma.fwhm = 3.0;
    ma.snr = 20.0;
    ma.eccentricity = 0.4;
    ma.succeeded = true;

    FrameQualityMetrics mb;
    mb.stars = 1500;
    mb.fwhm = 2.4;
    mb.snr = 60.0;
    mb.eccentricity = 0.2;
    mb.succeeded = true;

    model.setFrameQualityMetrics(a.id, ma);
    model.setFrameQualityMetrics(b.id, mb);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_FALSE(group.normalizationReferenceFrames.empty());
    EXPECT_EQ(group.normalizationReferenceFrames.front().canonicalPath(), b.id.canonicalPath());
}

TEST(ManifestBuilder, NormalizationReferenceUsesTransparencySignal)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("norm_ref_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("norm_ref_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));

    model.addFrames(FrameType::Light, {a, b});

    FrameQualityMetrics ma;
    ma.stars = 1200;
    ma.fwhm = 2.2;
    ma.snr = 40.0;
    ma.eccentricity = 0.20;
    ma.succeeded = true;

    FrameQualityMetrics mb = ma;
    mb.photometric_zero_point = 24.5;
    mb.catalog_matched_stars = 140;

    model.setFrameQualityMetrics(a.id, ma);
    model.setFrameQualityMetrics(b.id, mb);

    ManifestBuilder builder(model);
    const WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    ASSERT_FALSE(manifest.integrationGroups.front().normalizationReferenceFrames.empty());
    EXPECT_EQ(manifest.integrationGroups.front().normalizationReferenceFrames.front().canonicalPath(),
              b.id.canonicalPath());
}

TEST(ManifestBuilder, NormalizationReferenceHonorsManualSelection)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("light_a.fit"), QSize(100, 100), 60.0, QStringLiteral("L"),
                            QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("light_b.fit"), QSize(100, 100), 60.0, QStringLiteral("L"),
                            QStringLiteral("M42"), QStringLiteral("2025-01-01"));

    model.addFrames(FrameType::Light, {a, b});

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Global;
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = a.id.canonicalPath();
    model.setStackingSettings(FrameType::Light, group_key, settings);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_FALSE(group.normalizationReferenceFrames.empty());
    EXPECT_EQ(group.normalizationReferenceFrames.front().canonicalPath(), a.id.canonicalPath());
}

// A reference chosen by hand that the group does not hold - left out of its stack, or renamed - is kept, so the stages
// refuse the group naming it. The builder fell back to the automatic choice and said so only in the developer log,
// while the run log still called the reference manual.
TEST(ManifestBuilder, ANormalizationReferenceChosenByHandIsKeptWhenTheGroupDoesNotHoldIt)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("manual_ref_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("manual_ref_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo c = makeFrame(FrameType::Light, QStringLiteral("manual_ref_c.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    model.addFrames(FrameType::Light, {a, b, c});
    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Global;
    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = a.id.canonicalPath();
    model.setStackingSettings(FrameType::Light, group_key, settings);
    model.setFramesStackExcluded(group_key, {a.id.canonicalPath()}, true);

    const WorkflowManifest excluded = ManifestBuilder(model).build();
    ASSERT_EQ(excluded.integrationGroups.size(), 1u);
    const auto& group = excluded.integrationGroups.front();
    ASSERT_EQ(group.lightFiles.size(), 2u) << "the reference is left out of the stack";
    ASSERT_EQ(group.normalizationReferenceFrames.size(), 1u);
    EXPECT_EQ(group.normalizationReferenceFrames.front(), a.id);

    const QString renamed = QDir(QDir::tempPath()).filePath(QStringLiteral("manual_ref_renamed.fit"));
    settings.manualNormalizationReference = renamed;
    model.setStackingSettings(FrameType::Light, group_key, settings);
    const WorkflowManifest missing = ManifestBuilder(model).build();
    ASSERT_EQ(missing.integrationGroups.size(), 1u);
    ASSERT_EQ(missing.integrationGroups.front().normalizationReferenceFrames.size(), 1u);
    EXPECT_EQ(missing.integrationGroups.front().normalizationReferenceFrames.front(), FrameId(renamed));
}

// The light a group's stack weighs against is the reference the manifest names the stacking stage - the best by the
// registration's metrics, or the light chosen by hand - and, when the group normalizes nothing, its first light by
// identity, which the stage takes when the manifest names none. The Frame Quality dashboard's preview weighs against
// it.
TEST(ManifestBuilder, TheStackingReferenceIsTheLightTheStageWeighsAgainst)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("stack_ref_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("stack_ref_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo c = makeFrame(FrameType::Light, QStringLiteral("stack_ref_c.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    model.addFrames(FrameType::Light, {a, b, c});
    const auto metricsOf = [](const int stars, const double fwhm, const double snr) {
        FrameQualityMetrics metrics;
        metrics.stars = stars;
        metrics.fwhm = fwhm;
        metrics.snr = snr;
        metrics.eccentricity = 0.2;
        metrics.succeeded = true;
        return metrics;
    };
    model.setFrameQualityMetrics(a.id, metricsOf(300, 3.2, 18.0));
    model.setFrameQualityMetrics(b.id, metricsOf(1800, 2.0, 65.0));
    model.setFrameQualityMetrics(c.id, metricsOf(900, 2.4, 40.0));
    const GroupKey groupKey = model.integrationGroups().front();
    const IntegrationGroupKey integrationKey{groupKey};

    const auto referenceTheStageReads = [&model]() {
        const WorkflowManifest manifest = ManifestBuilder(model).build();
        EXPECT_EQ(manifest.integrationGroups.size(), 1u);
        if (manifest.integrationGroups.empty())
        {
            return FrameId();
        }
        const WorkflowManifest::IntegrationGroup& group = manifest.integrationGroups.front();
        if (!group.normalizationReferenceFrames.empty())
        {
            return group.normalizationReferenceFrames.front();
        }
        // The stage's own fallback: the first of the group's lights, which the manifest holds by identity.
        return *std::min_element(group.lightFiles.begin(), group.lightFiles.end());
    };

    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Global;
    model.setStackingSettings(FrameType::Light, groupKey, settings);
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(b.id));
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(referenceTheStageReads()));

    settings.normalizationReferenceMode = NormalizationReferenceMode::Manual;
    settings.manualNormalizationReference = c.id.canonicalPath();
    model.setStackingSettings(FrameType::Light, groupKey, settings);
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(c.id));
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(referenceTheStageReads()));

    settings.normalizationMode = NormalizationMode::None;
    model.setStackingSettings(FrameType::Light, groupKey, settings);
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>((std::min)({a.id, b.id, c.id})));
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(referenceTheStageReads()));

    // A light left out of this stack is not its reference.
    model.setFramesStackExcluded(groupKey, {(std::min)({a.id, b.id, c.id}).canonicalPath()}, true);
    EXPECT_EQ(stackingReferenceFrame(model, integrationKey), std::optional<FrameId>(referenceTheStageReads()));

    EXPECT_FALSE(stackingReferenceFrame(model, IntegrationGroupKey{GroupKey{QStringLiteral("no such group")}}));
}

TEST(ManifestBuilder, LocalNormalizationIntegratesBestFramesForIntegrationAlias)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("ln_alias_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("ln_alias_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo c = makeFrame(FrameType::Light, QStringLiteral("ln_alias_c.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));

    model.addFrames(FrameType::Light, {a, b, c});

    FrameQualityMetrics ma;
    ma.stars = 300;
    ma.fwhm = 3.2;
    ma.snr = 18.0;
    ma.eccentricity = 0.4;
    ma.succeeded = true;

    FrameQualityMetrics mb;
    mb.stars = 1800;
    mb.fwhm = 2.0;
    mb.snr = 65.0;
    mb.eccentricity = 0.2;
    mb.succeeded = true;

    FrameQualityMetrics mc;
    mc.stars = 900;
    mc.fwhm = 2.4;
    mc.snr = 40.0;
    mc.eccentricity = 0.25;
    mc.succeeded = true;

    model.setFrameQualityMetrics(a.id, ma);
    model.setFrameQualityMetrics(b.id, mb);
    model.setFrameQualityMetrics(c.id, mc);

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.referenceStrategy = QStringLiteral("Integration Best Frames");
    settings.localNormalization.maxIntegratedFrames = 2;
    model.setStackingSettings(FrameType::Light, group_key, settings);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_EQ(group.normalizationReferenceFrames.size(), 2u);
    EXPECT_EQ(group.normalizationReferenceFrames[0].canonicalPath(), b.id.canonicalPath());
    EXPECT_EQ(group.normalizationReferenceFrames[1].canonicalPath(), c.id.canonicalPath());
}

TEST(ManifestBuilder, LocalNormalizationEvaluationCriteriaStarCountIsHonored)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    FrameInfo a = makeFrame(FrameType::Light, QStringLiteral("ln_criteria_a.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo b = makeFrame(FrameType::Light, QStringLiteral("ln_criteria_b.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));
    FrameInfo c = makeFrame(FrameType::Light, QStringLiteral("ln_criteria_c.fit"), QSize(100, 100), 60.0,
                            QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"));

    model.addFrames(FrameType::Light, {a, b, c});

    // b has the highest star count but not the best default quality score.
    FrameQualityMetrics ma;
    ma.stars = 900;
    ma.fwhm = 1.8;
    ma.snr = 80.0;
    ma.eccentricity = 0.10;
    ma.succeeded = true;

    FrameQualityMetrics mb;
    mb.stars = 2000;
    mb.fwhm = 3.0;
    mb.snr = 10.0;
    mb.eccentricity = 0.55;
    mb.succeeded = true;

    FrameQualityMetrics mc;
    mc.stars = 1100;
    mc.fwhm = 2.2;
    mc.snr = 45.0;
    mc.eccentricity = 0.20;
    mc.succeeded = true;

    model.setFrameQualityMetrics(a.id, ma);
    model.setFrameQualityMetrics(b.id, mb);
    model.setFrameQualityMetrics(c.id, mc);

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.referenceStrategy = QStringLiteral("Integrate Best Frames");
    settings.localNormalization.maxIntegratedFrames = 1;
    settings.localNormalization.evaluationCriteria = QStringLiteral("Star Count");
    model.setStackingSettings(FrameType::Light, group_key, settings);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_EQ(group.normalizationReferenceFrames.size(), 1u);
    EXPECT_EQ(group.normalizationReferenceFrames.front().canonicalPath(), b.id.canonicalPath());
}

TEST(ManifestBuilder, ValidatesOutputPaths)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    WorkflowValidationResult validation = manifest.validate();
    EXPECT_TRUE(validation.hasErrors());
}

TEST(ManifestBuilder, HandlesIncrementalMode)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest initial = builder.build();
    ASSERT_EQ(initial.integrationGroups.size(), 1u);

    const QString output_path = QDir(temp_dir.path()).filePath(initial.integrationGroups.front().outputFilename);
    QFile file(output_path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("test");
    file.close();

    WorkflowManifest incremental = builder.build();

    builder.setReprocessExisting(true);
    WorkflowManifest full = builder.build();

    ASSERT_EQ(incremental.integrationGroups.size(), 1u);
    ASSERT_EQ(full.integrationGroups.size(), 1u);
    EXPECT_TRUE(incremental.integrationGroups.front().skipIfExists);
    EXPECT_FALSE(full.integrationGroups.front().skipIfExists);
}

TEST(ManifestBuilder, EnablesDrizzleFromStackingSettings)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const GroupKey group_key = model.integrationGroups().front();
    StackingSettings settings;
    settings.drizzle.enabled = true;
    settings.drizzle.scale = 2.5;
    model.setStackingSettings(FrameType::Light, group_key, settings);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const auto& group = manifest.integrationGroups.front();
    ASSERT_TRUE(group.drizzle.has_value());
    EXPECT_TRUE(group.drizzle->enabled);
    EXPECT_FLOAT_EQ(group.drizzle->scale, 2.5f);
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsDrizzle")));
}

TEST(ManifestBuilder, RespectsIncludeFrameTypeFlags)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    builder.setIncludeBiasFrames(false);
    WorkflowManifest manifest = builder.build();
    EXPECT_FALSE(manifest.biasStage.has_value());
}

TEST(ManifestBuilder, ExcludesLightsCalibrationWhenInMemory)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    // In-Memory Mode
    model.setLightCalibrationCacheMode(LightCalibrationCacheMode::InMemory);

    // Add frames to trigger calibration logic
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Verification
    EXPECT_TRUE(manifest.calibrateLightsInMemory);
    EXPECT_FALSE(manifest.stageSequence.contains(QStringLiteral("LightsCalibration")));

    // Check that LightsRegistration is still there, implying we moved calibration logic
    EXPECT_TRUE(manifest.stageSequence.contains(QStringLiteral("LightsRegistration")));

    // Verify lightsCalibration params are still generated (needed for in-memory mapping)
    EXPECT_FALSE(manifest.lightsCalibration.empty());
}

TEST(WorkflowManifest, SerializesToValidJson)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    QJsonObject json = manifest.toJson();

    EXPECT_TRUE(json.contains(QStringLiteral("version")));
    EXPECT_TRUE(json.contains(QStringLiteral("outputDirectory")));
    EXPECT_TRUE(json.value(QStringLiteral("integrationGroups")).isArray());
    EXPECT_TRUE(json.value(QStringLiteral("registrationGroups")).isArray());
}

TEST(WorkflowManifest, MaxParallelCoresRoundTrips)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("parallel.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    manifest.maxParallelCores = 3;

    const QJsonObject json = manifest.toJson();
    EXPECT_EQ(json.value(QStringLiteral("maxParallelCores")).toInt(), 3);

    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);
    EXPECT_EQ(parsed.maxParallelCores, 3);
}

TEST(WorkflowManifest, CombineRgbImagesRoundTrips)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("combine_rgb.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    manifest.combineRgbImages = true;

    const QJsonObject json = manifest.toJson();
    EXPECT_TRUE(json.value(QStringLiteral("combineRgbImages")).toBool());

    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);
    EXPECT_TRUE(parsed.combineRgbImages);
}

TEST(WorkflowManifest, SerializesRegistrationAutoReferenceDuringRegistrationSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(manifest.registrationGroups.empty());

    manifest.registrationGroups.front().settings.autoReferenceDuringRegistration = false;
    const QJsonObject json = manifest.toJson();

    const QJsonArray reg_groups = json.value(QStringLiteral("registrationGroups")).toArray();
    ASSERT_EQ(reg_groups.size(), 1);
    const QJsonObject reg_group = reg_groups.at(0).toObject();
    const QJsonObject settings = reg_group.value(QStringLiteral("settings")).toObject();

    EXPECT_TRUE(settings.contains(QStringLiteral("autoReferenceDuringRegistration")));
    EXPECT_FALSE(settings.value(QStringLiteral("autoReferenceDuringRegistration")).toBool(true));
}

TEST(WorkflowManifest, SerializesRegistrationAutoSolveUnsolvedFramesSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(manifest.registrationGroups.empty());

    manifest.registrationGroups.front().settings.autoSolveUnsolvedFrames = true;
    const QJsonObject json = manifest.toJson();

    const QJsonArray reg_groups = json.value(QStringLiteral("registrationGroups")).toArray();
    ASSERT_EQ(reg_groups.size(), 1);
    const QJsonObject reg_group = reg_groups.at(0).toObject();
    const QJsonObject settings = reg_group.value(QStringLiteral("settings")).toObject();

    EXPECT_TRUE(settings.contains(QStringLiteral("autoSolveUnsolvedFrames")));
    EXPECT_TRUE(settings.value(QStringLiteral("autoSolveUnsolvedFrames")).toBool(false));
}

TEST(WorkflowManifest, SerializesRegistrationGaiaEnrichmentSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(manifest.registrationGroups.empty());

    manifest.registrationGroups.front().settings.enrichWithGaiaCatalog = false;
    const QJsonObject json = manifest.toJson();
    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);

    const QJsonArray reg_groups = json.value(QStringLiteral("registrationGroups")).toArray();
    ASSERT_EQ(reg_groups.size(), 1);
    const QJsonObject reg_group = reg_groups.at(0).toObject();
    const QJsonObject settings = reg_group.value(QStringLiteral("settings")).toObject();

    EXPECT_TRUE(settings.contains(QStringLiteral("enrichWithGaiaCatalog")));
    EXPECT_FALSE(settings.value(QStringLiteral("enrichWithGaiaCatalog")).toBool(true));
    ASSERT_FALSE(parsed.registrationGroups.empty());
    EXPECT_FALSE(parsed.registrationGroups.front().settings.enrichWithGaiaCatalog);
}

// A manifest that states no format version reads as format zero, which is not this version's: nothing that reads a
// stored manifest reuses it. One this version wrote reads as its own.
TEST(WorkflowManifest, AManifestThatStatesNoFormatIsNotThisVersions)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("unversioned.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    QJsonObject json = manifest.toJson();
    EXPECT_TRUE(WorkflowManifest::fromJson(json).isCurrentFormat());

    json.remove(QStringLiteral("version"));

    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);
    EXPECT_EQ(parsed.version, 0);
    EXPECT_FALSE(parsed.isCurrentFormat());
}

TEST(WorkflowManifest, FromJsonPreservesFutureManifestVersion)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("future.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({frame_a});
    QJsonObject json = manifest.toJson();
    json.insert(QStringLiteral("version"), 7);

    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);
    EXPECT_EQ(parsed.version, 7);
    EXPECT_EQ(parsed.toJson().value(QStringLiteral("version")).toInt(), 7);
}

TEST(WorkflowManifest, RoundTripsWithoutLoss)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();
    WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());

    const auto first_json = QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact);
    const auto second_json = QJsonDocument(round_trip.toJson()).toJson(QJsonDocument::Compact);

    EXPECT_EQ(first_json, second_json);
}

TEST(WorkflowManifest, ValidatesSchema)
{
    WorkflowManifest manifest;
    manifest.version = 0;

    WorkflowValidationResult validation = manifest.validate();
    EXPECT_TRUE(validation.hasErrors());
}

TEST(WorkflowManifest, RejectsDuplicateLightCalibrationFrameIdentities)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QStringLiteral("D:/Output");

    WorkflowManifest::LightsCalibrationParams first;
    first.lightPath = FrameId(QStringLiteral("D:/Lights/duplicate.fit"));
    WorkflowManifest::LightsCalibrationParams duplicate = first;
    manifest.lightsCalibration = {first, duplicate};

    const WorkflowValidationResult validation = manifest.validate();

    EXPECT_TRUE(validation.hasErrors());
    EXPECT_TRUE(
        hasValidationIssue(validation, FixTarget::CalibrationTab, QStringLiteral("Duplicate light calibration entry")));
}

/// A manifest whose light group's stacking settings hold `text` in the field `set` writes, validated: the issue a text
/// no table knows raises, which the stacking stage would otherwise raise once the run had started.
WorkflowValidationResult validateLightStacking(const std::function<void(StackingSettings&)>& set)
{
    WorkflowManifest manifest = makeManifestWithFrames(
        {FrameId(QStringLiteral("/tmp/unknown_choice_a.fit")), FrameId(QStringLiteral("/tmp/unknown_choice_b.fit"))});
    set(manifest.integrationGroups.front().stacking);
    return manifest.validate();
}

TEST(WorkflowManifest, AStackingMethodNoTableKnowsIsRefusedBeforeTheRun)
{
    const WorkflowValidationResult validation =
        validateLightStacking([](StackingSettings& s) { s.stackMethod = QStringLiteral("Trimmed Mean"); });
    EXPECT_TRUE(hasValidationIssue(validation, FixTarget::IntegrationTab,
                                   QStringLiteral("Unknown stacking method 'Trimmed Mean'")))
        << validationMessages(validation).toStdString();
    EXPECT_FALSE(hasValidationIssue(validateLightStacking([](StackingSettings&) {}), FixTarget::IntegrationTab,
                                    QStringLiteral("Unknown")));

    // A calibration group's master is stacked by its method and rejection too.
    WorkflowManifest calibration = makeManifestWithFlatCalibrationGroup();
    calibration.flatStage->groups.front().stacking.stackMethod = QStringLiteral("Trimmed Mean");
    const WorkflowValidationResult master = calibration.validate();
    EXPECT_TRUE(
        hasValidationIssue(master, FixTarget::CalibrationTab, QStringLiteral("Unknown stacking method 'Trimmed Mean'")))
        << validationMessages(master).toStdString();
}

TEST(WorkflowManifest, ARejectionMethodNoTableKnowsIsRefusedBeforeTheRun)
{
    const WorkflowValidationResult validation =
        validateLightStacking([](StackingSettings& s) { s.rejectionMethod = QStringLiteral("Linear Fit Clipping"); });
    EXPECT_TRUE(hasValidationIssue(validation, FixTarget::IntegrationTab,
                                   QStringLiteral("Unknown rejection method 'Linear Fit Clipping'")))
        << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, AWeightingNoTableKnowsIsRefusedBeforeTheRun)
{
    const WorkflowValidationResult validation =
        validateLightStacking([](StackingSettings& s) { s.weighting = QStringLiteral("Exposure"); });
    EXPECT_TRUE(
        hasValidationIssue(validation, FixTarget::IntegrationTab, QStringLiteral("Unknown weighting 'Exposure'")))
        << validationMessages(validation).toStdString();
}

TEST(WorkflowManifest, AStarCountScalingNoTableKnowsIsRefusedBeforeTheRun)
{
    const WorkflowValidationResult validation =
        validateLightStacking([](StackingSettings& s) { s.starCountScaling = QStringLiteral("Cubic"); });
    EXPECT_TRUE(
        hasValidationIssue(validation, FixTarget::IntegrationTab, QStringLiteral("Unknown star count scaling 'Cubic'")))
        << validationMessages(validation).toStdString();
}

// The drizzle's scale and drop size reached the engine through a float, so 0.9 was drizzled as 0.8999999761581421.
TEST(WorkflowManifest, TheDrizzleScaleAndPixelFractionKeepTheValueTheGroupStates)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("drizzle_fraction_a.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Light, QStringLiteral("drizzle_fraction_b.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});
    ASSERT_FALSE(model.integrationGroups().empty());
    StackingSettings drizzled;
    drizzled.drizzle.enabled = true;
    drizzled.drizzle.scale = 1.5;
    drizzled.drizzle.pixelFraction = 0.9;
    model.setStackingSettings(FrameType::Light, model.integrationGroups().front(), drizzled);

    const WorkflowManifest manifest = ManifestBuilder(model).build();
    ASSERT_FALSE(manifest.integrationGroups.empty());
    const auto& drizzle = manifest.integrationGroups.front().drizzle;
    ASSERT_TRUE(drizzle.has_value());
    EXPECT_EQ(drizzle->pixelFraction, 0.9);
    EXPECT_EQ(drizzle->scale, 1.5);

    const WorkflowManifest read = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_TRUE(read.integrationGroups.front().drizzle.has_value());
    EXPECT_EQ(read.integrationGroups.front().drizzle->pixelFraction, 0.9);
    EXPECT_EQ(read.integrationGroups.front().drizzle->scale, 1.5);
}

TEST(ManifestHashing, StarsPlusCometExperimentalTokenDoesNotAliasAnUnofferedModeText)
{
    StackingSettings unoffered_settings;
    unoffered_settings.cometStackingMode = QStringLiteral("Stars+Comet");

    StackingSettings experimental_settings;
    experimental_settings.cometStackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();

    EXPECT_NE(computeStackingSettingsHash(unoffered_settings), computeStackingSettingsHash(experimental_settings));

    WorkflowManifest previous =
        makeManifestWithFrames({FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_unoffered.fit"))});
    WorkflowManifest current = previous;
    previous.integrationGroups.front().stacking = unoffered_settings;
    current.integrationGroups.front().stacking = experimental_settings;

    const ManifestDiff diff = compareManifests(previous, current);
    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    EXPECT_EQ(
        diff.integrationChanges.front().settingsChanged.stages(),
        (LightStages{LightStage::Alignment, LightStage::Normalization, LightStage::Drizzle, LightStage::Stacking}));
}

TEST(ManifestHashing, IntegrationGroupHashChangesWhenCombineRgbImagesChanges)
{
    WorkflowManifest disabled =
        makeManifestWithFrames({FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_rgb_a.fit")),
                                FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_rgb_b.fit"))});
    WorkflowManifest enabled = disabled;
    enabled.combineRgbImages = true;

    ASSERT_FALSE(disabled.integrationGroups.empty());
    ASSERT_FALSE(enabled.integrationGroups.empty());
    EXPECT_NE(computeIntegrationGroupHash(disabled, disabled.integrationGroups.front()),
              computeIntegrationGroupHash(enabled, enabled.integrationGroups.front()));

    const ManifestDiff diff = compareManifests(disabled, enabled);
    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    EXPECT_EQ(diff.integrationChanges.front().settingsChanged.stages(), LightStages{LightStage::Stacking});
    EXPECT_EQ(diff.integrationChanges.front().settingsChanged.stacking,
              QStringList{QStringLiteral("Combine RGB images changed")});
}

// A master is reused while its group's settings hash to the identity it was stacked under, and the stacking settings
// hash into the identity of every master - each calibration master and each light master - so a setting retired from
// them leaves the identity they hash to as it was: no master is stacked again for it. The values are the identities
// these settings have hashed to since the hash took the per-channel width weighting they no longer hold.
TEST(ManifestHashing, TheStackingSettingsKeepTheIdentityTheirMastersWereStackedUnder)
{
    const std::string defaults = computeStackingSettingsHash(StackingSettings{}).toStdString();
    EXPECT_EQ(defaults, "45dc42eee7692ba1642fda9b201af0df9f5d3006dce47bdb1f0c318a0d709d15");

    StackingSettings width;
    width.weighting = QStringLiteral("Weighted FWHM");
    width.minWeightFloor = 0.05;
    const std::string weighed = computeStackingSettingsHash(width).toStdString();
    EXPECT_EQ(weighed, "ae98326b8e1a59b5fea5b24fcf0298ff6aeb0c7809eef101bc293087fa3a4e82");
}

// A manifest states no per-channel width weighting: a group's stacking settings are written without one, and a
// manifest an earlier version wrote with the choice on reads as these settings and is written again without it. The
// choice was written, read back and kept, though no stack could honour it.
TEST(WorkflowManifest, AStoredPerChannelWidthWeightingIsLetGo)
{
    const QString retired = QStringLiteral("perChannelFwhmWeighting");
    const auto groupStackingOf = [](const QJsonObject& json) {
        const QJsonArray groups = json.value(QStringLiteral("integrationGroups")).toArray();
        return groups.isEmpty() ? QJsonObject{} : groups.at(0).toObject().value(QStringLiteral("stacking")).toObject();
    };

    WorkflowManifest manifest =
        makeManifestWithFrames({FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("retired_choice.fit"))});
    ASSERT_FALSE(manifest.integrationGroups.empty());
    manifest.integrationGroups.front().stacking.weighting = QStringLiteral("Weighted FWHM");

    QJsonObject json = manifest.toJson();
    QJsonObject stacking = groupStackingOf(json);
    ASSERT_FALSE(stacking.isEmpty());
    EXPECT_FALSE(stacking.contains(retired));

    stacking.insert(retired, true);
    QJsonArray groups = json.value(QStringLiteral("integrationGroups")).toArray();
    QJsonObject group = groups.at(0).toObject();
    group.insert(QStringLiteral("stacking"), stacking);
    groups[0] = group;
    json.insert(QStringLiteral("integrationGroups"), groups);

    const WorkflowManifest read = WorkflowManifest::fromJson(json);
    ASSERT_FALSE(read.integrationGroups.empty());
    EXPECT_EQ(read.integrationGroups.front().stacking.weighting, QStringLiteral("Weighted FWHM"));
    EXPECT_FALSE(groupStackingOf(read.toJson()).contains(retired));
}

TEST(ManifestDiff, DetectsAddedFrames)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("b.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a});
    WorkflowManifest current = makeManifestWithFrames({frame_a, frame_b});

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    const auto& change = diff.integrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_TRUE(change.lightFilesChanged);
    EXPECT_EQ(change.previousFrameCount, 1);
    EXPECT_EQ(change.currentFrameCount, 2);
}

TEST(ManifestDiff, DetectsAutoReferenceDuringRegistrationSettingChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a});
    WorkflowManifest current = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(previous.registrationGroups.empty());
    ASSERT_FALSE(current.registrationGroups.empty());

    current.registrationGroups.front().settings.autoReferenceDuringRegistration =
        !previous.registrationGroups.front().settings.autoReferenceDuringRegistration;

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    const auto& change = diff.registrationChanges.front();
    EXPECT_TRUE(change.settingsChanged);
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
}

TEST(ManifestDiff, DetectsAutoSolveUnsolvedFramesSettingChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a});
    WorkflowManifest current = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(previous.registrationGroups.empty());
    ASSERT_FALSE(current.registrationGroups.empty());

    current.registrationGroups.front().settings.autoSolveUnsolvedFrames =
        !previous.registrationGroups.front().settings.autoSolveUnsolvedFrames;

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    const auto& change = diff.registrationChanges.front();
    EXPECT_TRUE(change.settingsChanged);
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
}

TEST(ManifestDiff, DetectsPlateSolveSettingChangeAsRegistrationChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a});
    WorkflowManifest current = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(previous.registrationGroups.empty());
    ASSERT_FALSE(current.registrationGroups.empty());

    current.plateSolve.defaultSolver = QStringLiteral("astrometry.net");

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    const auto& change = diff.registrationChanges.front();
    EXPECT_TRUE(change.settingsChanged);
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
}

TEST(ManifestDiff, DetectsPlateSolveApiKeyHashChangeAsRegistrationChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a});
    WorkflowManifest current = makeManifestWithFrames({frame_a});
    ASSERT_FALSE(previous.registrationGroups.empty());
    ASSERT_FALSE(current.registrationGroups.empty());

    previous.astrometryApiKeyHash = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    current.astrometryApiKeyHash = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    const auto& change = diff.registrationChanges.front();
    EXPECT_TRUE(change.settingsChanged);
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
}

TEST(ManifestDiff, DetectsCometTrackingSettingChangeAsRegistrationChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("b.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a, frame_b});
    WorkflowManifest current = makeManifestWithFrames({frame_a, frame_b});
    ASSERT_FALSE(previous.registrationGroups.empty());
    ASSERT_FALSE(current.registrationGroups.empty());

    previous.registrationGroups.front().settings.registrationMethod = QStringLiteral("comet_tracking");
    RegistrationSettings::CometTrackingSettings comet_settings;
    comet_settings.observations = {
        RegistrationSettings::CometTrackingObservation{frame_a.canonicalPath(), QPointF(10.0, 10.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{frame_b.canonicalPath(), QPointF(20.0, 12.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true}};
    previous.registrationGroups.front().settings.cometTracking = comet_settings;

    current.registrationGroups.front().settings = previous.registrationGroups.front().settings;
    current.registrationGroups.front().settings.cometTracking->observations[1].sourcePosition = QPointF(22.0, 11.0);

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.registrationChanges.size(), 1u);
    const auto& change = diff.registrationChanges.front();
    EXPECT_TRUE(change.settingsChanged);
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
}

TEST(ManifestDiff, DetectsRemovedFrames)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("b.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a, frame_b});
    WorkflowManifest current = makeManifestWithFrames({frame_a});

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    const auto& change = diff.integrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_TRUE(change.lightFilesChanged);
    EXPECT_EQ(change.previousFrameCount, 2);
    EXPECT_EQ(change.currentFrameCount, 1);
}

TEST(ManifestDiff, DetectsCalibrationDependencyFieldChanges)
{
    const auto expect_modified = [](const char* field_name, auto mutate_group) {
        SCOPED_TRACE(field_name);

        WorkflowManifest previous = makeManifestWithFlatCalibrationGroup();
        WorkflowManifest current = previous;
        ASSERT_TRUE(current.flatStage.has_value());
        ASSERT_EQ(current.flatStage->groups.size(), 1u);

        mutate_group(current.flatStage->groups.front());

        const ManifestDiff diff = compareManifests(previous, current);

        ASSERT_EQ(diff.calibrationChanges.size(), 1u);
        const auto& change = diff.calibrationChanges.front();
        EXPECT_EQ(change.frameType, QStringLiteral("FLAT"));
        EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
        EXPECT_TRUE(change.calibrationDependenciesChanged);
    };

    expect_modified("existingMaster", [](auto& group) {
        group.existingMaster = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("existing_master.fit"));
    });
    expect_modified("biasMaster", [](auto& group) {
        group.biasMaster = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("new_master_bias.fit"));
    });
    expect_modified("darkMaster", [](auto& group) {
        group.darkMaster = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("new_master_dark.fit"));
    });
}

TEST(ManifestDiff, IgnoresCalibrationSkipPolicyChanges)
{
    WorkflowManifest previous = makeManifestWithFlatCalibrationGroup();
    WorkflowManifest current = previous;
    ASSERT_TRUE(previous.flatStage.has_value());
    ASSERT_TRUE(current.flatStage.has_value());

    current.flatStage->groups.front().skipIfExists = !previous.flatStage->groups.front().skipIfExists;

    EXPECT_EQ(computeCalibrationGroupHash(previous, previous.flatStage->groups.front()),
              computeCalibrationGroupHash(current, current.flatStage->groups.front()));

    const ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.calibrationChanges.size(), 1u);
    const auto& change = diff.calibrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged);
    EXPECT_FALSE(change.calibrationDependenciesChanged);
    EXPECT_FALSE(change.calibrationFingerprintChanged);
    EXPECT_EQ(diff.totalModifications(), 0);
}

TEST(ManifestDiff, DoesNotMarkCalibrationModifiedForUnrelatedPlateSolveChange)
{
    WorkflowManifest previous = makeManifestWithFlatCalibrationGroup();
    WorkflowManifest current = previous;
    current.plateSolve.defaultSolver = QStringLiteral("internal_gaia");

    const ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.calibrationChanges.size(), 1u);
    EXPECT_EQ(diff.calibrationChanges.front().change, ManifestDiff::CalibrationGroupChange::ChangeType::Unchanged);
    EXPECT_EQ(diff.totalModifications(), 0);
}

TEST(ManifestDiff, DetectsCalibrationNormalizedHashOnlyChanges)
{
    WorkflowManifest previous = makeManifestWithFlatCalibrationGroup();
    WorkflowManifest current = previous;
    current.outputDirectory = QStringLiteral("D:/OtherOutput");

    ASSERT_TRUE(previous.flatStage.has_value());
    ASSERT_TRUE(current.flatStage.has_value());
    ASSERT_NE(computeCalibrationGroupHash(previous, previous.flatStage->groups.front()),
              computeCalibrationGroupHash(current, current.flatStage->groups.front()));

    const ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.calibrationChanges.size(), 1u);
    const auto& change = diff.calibrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_TRUE(change.calibrationFingerprintChanged);
    EXPECT_FALSE(change.inputFilesChanged);
    EXPECT_FALSE(change.stackingSettingsChanged);
    EXPECT_FALSE(change.outputFilenameChanged);
}

TEST(ManifestDiff, DetectsFinalMasterFallbackSettingChange)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("stack_a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("stack_b.fit"));

    WorkflowManifest previous = makeManifestWithFrames({frame_a, frame_b});
    previous.integrationGroups.front().stacking.solveFinalMasterIfNeeded = true;
    WorkflowManifest current = previous;
    current.integrationGroups.front().stacking.solveFinalMasterIfNeeded = false;

    ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    const auto& change = diff.integrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_EQ(change.settingsChanged.stages(), LightStages{LightStage::Stacking});
}
// =============================================================================
// Pre-Calibrated Light Groups Tests
// =============================================================================

TEST(ManifestBuilder, PreCalibratedGroup_SkipsLightsCalibration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add calibration frames
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("L"), QString(), QStringLiteral("2025-01-01"))});

    // Add light frame
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    // Build manifest without pre-calibrated flag
    {
        ManifestBuilder builder(model);
        WorkflowManifest manifest = builder.build();

        // Should have lights calibration params
        EXPECT_FALSE(manifest.lightsCalibration.empty());
    }

    // Mark light group as pre-calibrated
    const auto& lightGroups = model.lightGroups();
    ASSERT_FALSE(lightGroups.empty());

    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model.setLightCalibrationSettings(lightGroups.front().key, settings);

    // Build manifest with pre-calibrated flag
    {
        ManifestBuilder builder(model);
        WorkflowManifest manifest = builder.build();

        // Should have empty lights calibration since the only light group is pre-calibrated
        EXPECT_TRUE(manifest.lightsCalibration.empty());

        // The group is recorded as pre-calibrated, so a report can tell it from a group no master matched.
        ASSERT_EQ(manifest.preCalibratedLightGroups.size(), 1u);
        EXPECT_EQ(manifest.preCalibratedLightGroups.front().groupKey, lightGroups.front().key);
        ASSERT_EQ(manifest.preCalibratedLightGroups.front().lightFiles.size(), 1u);
        EXPECT_EQ(manifest.preCalibratedLightGroups.front().lightFiles.front().canonicalPath(),
                  lightGroups.front().frames.front().id.canonicalPath());

        const WorkflowManifest restored = WorkflowManifest::fromJson(manifest.toJson());
        ASSERT_EQ(restored.preCalibratedLightGroups.size(), 1u);
        EXPECT_EQ(restored.preCalibratedLightGroups.front().groupKey, lightGroups.front().key);
        ASSERT_EQ(restored.preCalibratedLightGroups.front().lightFiles.size(), 1u);
        EXPECT_EQ(restored.preCalibratedLightGroups.front().lightFiles.front().canonicalPath(),
                  lightGroups.front().frames.front().id.canonicalPath());
    }
}

TEST(ManifestBuilder, PreCalibratedGroup_MixedGroups)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Enable session-based grouping to create separate light groups
    GroupingOptions lightOpts = model.groupingOptions(FrameType::Light);
    lightOpts.bySession = true;
    model.setGroupingOptions(FrameType::Light, lightOpts);

    // Add calibration frames
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});

    // Add light frames from two different sessions (will create two groups)
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light_s1.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01")),
                     makeFrame(FrameType::Light, QStringLiteral("light_s2.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-02"))});

    const auto& lightGroups = model.lightGroups();
    ASSERT_EQ(lightGroups.size(), 2u);

    // Mark only the first group as pre-calibrated
    LightCalibrationSettings preCalibratedSettings;
    preCalibratedSettings.preCalibrated = true;
    model.setLightCalibrationSettings(lightGroups.front().key, preCalibratedSettings);

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Should have exactly 1 light in calibration (the non-pre-calibrated one)
    EXPECT_EQ(manifest.lightsCalibration.size(), 1u);
    ASSERT_EQ(manifest.preCalibratedLightGroups.size(), 1u);
    EXPECT_EQ(manifest.preCalibratedLightGroups.front().groupKey, lightGroups.front().key);
}

// ============================================================================
// Manually Unassigned Calibration Tests
// ============================================================================

TEST(ManifestBuilder, UnassignedBias_NotIncludedInLightsCalibration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add matching bias and light frames
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const auto& lightGroups = model.lightGroups();
    ASSERT_EQ(lightGroups.size(), 1u);
    const LightGroupKey lightKey = LightGroupKey::fromFrame(lightGroups.front().frames.front());

    // Mark bias as unassigned for this light group
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Lights calibration entry should have no bias path
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    EXPECT_FALSE(manifest.lightsCalibration.front().biasPath.has_value());
}

TEST(ManifestBuilder, UnassignedDark_NotIncludedInLightsCalibration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add matching dark and light frames
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const auto& lightGroups = model.lightGroups();
    ASSERT_EQ(lightGroups.size(), 1u);
    const LightGroupKey lightKey = LightGroupKey::fromFrame(lightGroups.front().frames.front());

    // Mark dark as unassigned for this light group
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Lights calibration entry should have no dark path
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    EXPECT_FALSE(manifest.lightsCalibration.front().darkPath.has_value());
}

TEST(ManifestBuilder, UnassignedFlat_NotIncludedInLightsCalibration)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add matching flat and light frames
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("L"), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const auto& lightGroups = model.lightGroups();
    ASSERT_EQ(lightGroups.size(), 1u);
    const LightGroupKey lightKey = LightGroupKey::fromFrame(lightGroups.front().frames.front());

    // Mark flat as unassigned for this light group
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Flat,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Lights calibration entry should have no flat path
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    EXPECT_FALSE(manifest.lightsCalibration.front().flatPath.has_value());
}

TEST(ManifestBuilder, UnassignedAllCalibrations_EmptyPaths)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());

    // Add all calibration types and light frame
    model.addFrames(FrameType::Bias, {makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Dark, {makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(100, 100), 60.0,
                                                QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Flat, {makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(100, 100), 1.0,
                                                QStringLiteral("L"), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light,
                    {makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(100, 100), 60.0,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"))});

    const auto& lightGroups = model.lightGroups();
    ASSERT_EQ(lightGroups.size(), 1u);
    const LightGroupKey lightKey = LightGroupKey::fromFrame(lightGroups.front().frames.front());

    // Mark all calibrations as unassigned
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Bias,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Dark,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));
    model.setCalibrationOverride(lightKey, CalibrationOverrideType::Flat,
                                 QString::fromLatin1(kUnassignedCalibrationMarker));

    ManifestBuilder builder(model);
    WorkflowManifest manifest = builder.build();

    // Lights calibration entry should have no calibration paths at all
    ASSERT_EQ(manifest.lightsCalibration.size(), 1u);
    EXPECT_FALSE(manifest.lightsCalibration.front().biasPath.has_value());
    EXPECT_FALSE(manifest.lightsCalibration.front().darkPath.has_value());
    EXPECT_FALSE(manifest.lightsCalibration.front().flatPath.has_value());
}

TEST(ManifestHashing, LightsCalibrationHashForSubset_EmptyWhenNoMatchingParams)
{
    const QString base = QDir::cleanPath(QDir::tempPath() + QDir::separator() + QStringLiteral("an_hash_test"));

    const FrameId light1(base + QDir::separator() + QStringLiteral("light1.fit"));
    const FrameId otherLight(base + QDir::separator() + QStringLiteral("other_light.fit"));

    std::vector<WorkflowManifest::LightsCalibrationParams> allParams;

    const QString hash = computeLightsCalibrationHashForSubset(allParams, {light1});
    EXPECT_TRUE(hash.isEmpty());

    // Even if params exist, subset with non-matching light should yield empty.
    WorkflowManifest::LightsCalibrationParams p;
    p.lightPath = otherLight;
    p.biasPath = FrameId(base + QDir::separator() + QStringLiteral("bias.fit"));
    allParams.push_back(p);

    const QString hash2 = computeLightsCalibrationHashForSubset(allParams, {light1});
    EXPECT_TRUE(hash2.isEmpty());
}

TEST(ManifestHashing, LightsCalibrationHashForSubset_IsOrderIndependentAndSensitiveToAssignments)
{
    const QString base = QDir::cleanPath(QDir::tempPath() + QDir::separator() + QStringLiteral("an_hash_test"));

    const FrameId light1(base + QDir::separator() + QStringLiteral("light1.fit"));
    const FrameId light2(base + QDir::separator() + QStringLiteral("light2.fit"));
    const FrameId bias1(base + QDir::separator() + QStringLiteral("bias1.fit"));
    const FrameId bias2(base + QDir::separator() + QStringLiteral("bias2.fit"));

    WorkflowManifest::LightsCalibrationParams p1;
    p1.lightPath = light1;
    p1.biasPath = bias1;

    WorkflowManifest::LightsCalibrationParams p2;
    p2.lightPath = light2;
    p2.biasPath = bias2;

    const QString hashLight1_A = computeLightsCalibrationHashForSubset({p1, p2}, {light1});
    const QString hashLight1_B = computeLightsCalibrationHashForSubset({p2, p1}, {light1});
    EXPECT_FALSE(hashLight1_A.isEmpty());
    EXPECT_EQ(hashLight1_A, hashLight1_B);

    const QString hashLight12 = computeLightsCalibrationHashForSubset({p1, p2}, {light2, light1});
    EXPECT_NE(hashLight1_A, hashLight12);

    // Changing an assignment for a light in the subset must change the hash.
    p1.biasPath = bias2;
    const QString hashLight1_changed = computeLightsCalibrationHashForSubset({p1, p2}, {light1});
    EXPECT_NE(hashLight1_A, hashLight1_changed);
}

TEST(ManifestHashing, StackingSettingsHash_ChangesWhenMemorySettingsChange)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.limitMemory = false;
    settings.maxMemoryMb = 2048;
    const QString h1 = computeStackingSettingsHash(settings);
    ASSERT_FALSE(h1.isEmpty());

    settings.limitMemory = true;
    const QString h2 = computeStackingSettingsHash(settings);
    EXPECT_NE(h1, h2);

    settings.limitMemory = false;
    settings.maxMemoryMb = 512;
    const QString h3 = computeStackingSettingsHash(settings);
    EXPECT_NE(h1, h3);
}

TEST(ManifestHashing, StackingSettingsHash_ChangesWhenHdrExposureMergeChanges)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");

    const QString disabled_hash = computeStackingSettingsHash(settings);
    ASSERT_FALSE(disabled_hash.isEmpty());

    settings.hdr.enabled = true;
    const QString enabled_hash = computeStackingSettingsHash(settings);
    EXPECT_NE(disabled_hash, enabled_hash);
}

TEST(ManifestHashing, StackingSettingsHash_ChangesWhenFinalMasterFallbackChanges)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.solveFinalMasterIfNeeded = true;

    const QString enabled_hash = computeStackingSettingsHash(settings);
    ASSERT_FALSE(enabled_hash.isEmpty());

    settings.solveFinalMasterIfNeeded = false;
    const QString disabled_hash = computeStackingSettingsHash(settings);
    EXPECT_NE(enabled_hash, disabled_hash);
}

TEST(ManifestHashing, StackingSettingsHash_ChangesWhenSpatialWeightingChanges)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");

    const QString disabled_hash = computeStackingSettingsHash(settings);
    ASSERT_FALSE(disabled_hash.isEmpty());

    settings.localWeighting.enabled = true;
    settings.localWeighting.windowSize = 9;
    settings.localWeighting.gridSpacing = 48;
    settings.localWeighting.snrWeightExponent = 2.25;
    const QString enabled_hash = computeStackingSettingsHash(settings);
    EXPECT_NE(disabled_hash, enabled_hash);
}

TEST(ManifestHashing, StackingSettingsHash_ChangesWhenOverlapNormalizationChanges)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.normalizationMode = NormalizationMode::Global;

    const QString disabled_hash = computeStackingSettingsHash(settings);
    ASSERT_FALSE(disabled_hash.isEmpty());

    settings.overlapNormalization.enabled = true;
    settings.overlapNormalization.minOverlapPixels = 4096;
    settings.overlapNormalization.minOverlapFraction = 0.025;
    const QString enabled_hash = computeStackingSettingsHash(settings);
    EXPECT_NE(disabled_hash, enabled_hash);
}

TEST(ManifestHashing, OverlapNormalizationFingerprintPinsSparseGraphBehavior)
{
    OverlapNormalizationSettings settings;
    settings.enabled = true;
    settings.minOverlapPixels = 4096;
    settings.minOverlapFraction = 0.025;
    QJsonObject serialized;

    addOverlapNormalizationSettings(serialized, settings);

    // The graph the normalization stage solves is the reference-star spanning tree, and the fingerprint names it, so a
    // change of graph invalidates every overlap-normalized artifact.
    EXPECT_EQ(ImageIntegration::kWorkflowOverlapGraphPolicy, an::OverlapGraphPolicy::ReferenceStarSpanningTree);
    EXPECT_EQ(serialized.value(QStringLiteral("graphPolicy")).toString(),
              QString::fromLatin1(an::overlapGraphPolicyName(an::OverlapGraphPolicy::ReferenceStarSpanningTree)));
    EXPECT_FALSE(serialized.value(QStringLiteral("graphPolicy")).toString().isEmpty());

    OverlapNormalizationSettings changed = settings;
    changed.minOverlapPixels = 2048;
    QJsonObject changed_serialized;
    addOverlapNormalizationSettings(changed_serialized, changed);
    EXPECT_EQ(changed_serialized.value(QStringLiteral("graphPolicy")).toString(),
              serialized.value(QStringLiteral("graphPolicy")).toString());
    EXPECT_FALSE(changed_serialized == serialized);
}

TEST(ManifestHashing, StackingSettingsHash_IgnoresAutoRejectionProvenance)
{
    StackingSettings settings;
    settings.stackMethod = QStringLiteral("Average");
    settings.rejectionMethod = QStringLiteral("Sigma Clipping");

    const QString explicit_hash = computeStackingSettingsHash(settings);
    ASSERT_FALSE(explicit_hash.isEmpty());

    settings.requestedRejectionMethod = QStringLiteral("Auto");
    const QString auto_hash = computeStackingSettingsHash(settings);
    EXPECT_EQ(explicit_hash, auto_hash);
}

// The dialog spells a kernel "Lanczos3" and a session read once gave back "lanczos3": the same kernel, which neither
// the hashes nor the diff may read as a change, so a reload alone drizzled and stacked the group again.
TEST(ManifestHashing, TheDrizzleKernelIsFingerprintedByItsMeaningNotItsSpelling)
{
    StackingSettings dialog;
    dialog.drizzle.enabled = true;
    dialog.drizzle.kernel = QStringLiteral("Lanczos3");
    StackingSettings keyed = dialog;
    keyed.drizzle.kernel = QStringLiteral("lanczos3");
    EXPECT_EQ(computeStackingSettingsHash(dialog), computeStackingSettingsHash(keyed));

    WorkflowManifest previous = makeManifestWithFrames(
        {FrameId(QStringLiteral("/tmp/kernel_spelling_a.fit")), FrameId(QStringLiteral("/tmp/kernel_spelling_b.fit"))});
    previous.integrationGroups.front().stacking = dialog;
    previous.integrationGroups.front().drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    previous.integrationGroups.front().drizzle->enabled = true;
    previous.integrationGroups.front().drizzle->kernel = QStringLiteral("Lanczos3");
    WorkflowManifest current = previous;
    current.integrationGroups.front().stacking = keyed;
    current.integrationGroups.front().drizzle->kernel = QStringLiteral("lanczos3");
    EXPECT_EQ(computeIntegrationGroupHash(previous, previous.integrationGroups.front()),
              computeIntegrationGroupHash(current, current.integrationGroups.front()));
    const ManifestDiff same = compareManifests(previous, current);
    ASSERT_EQ(same.integrationChanges.size(), 1u);
    EXPECT_FALSE(same.integrationChanges.front().settingsChanged.any());

    // Another kernel is another fingerprint.
    WorkflowManifest other = current;
    other.integrationGroups.front().stacking.drizzle.kernel = QStringLiteral("Gaussian");
    other.integrationGroups.front().drizzle->kernel = QStringLiteral("Gaussian");
    EXPECT_NE(computeStackingSettingsHash(other.integrationGroups.front().stacking),
              computeStackingSettingsHash(keyed));
    EXPECT_NE(computeIntegrationGroupHash(other, other.integrationGroups.front()),
              computeIntegrationGroupHash(current, current.integrationGroups.front()));
    const ManifestDiff changed = compareManifests(current, other);
    ASSERT_EQ(changed.integrationChanges.size(), 1u);
    EXPECT_EQ(changed.integrationChanges.front().settingsChanged.stages(),
              (LightStages{LightStage::Drizzle, LightStage::Stacking}));
}

// A drizzled Local master is normalized by the normalization stage's document, read where each drizzled light lies on
// the plane it was measured on, where its stack fitted the surfaces itself before: only such a master's fingerprint
// states how, so it alone is stacked again, and every other group's fingerprint states what it stated.
TEST(ManifestHashing, OnlyADrizzledLocalGroupsFingerprintStatesHowItsSurfacesAreApplied)
{
    WorkflowManifest manifest = makeManifestWithFrames(
        {FrameId(QStringLiteral("/tmp/drizzled_local_a.fit")), FrameId(QStringLiteral("/tmp/drizzled_local_b.fit"))});
    WorkflowManifest::IntegrationGroup& group = manifest.integrationGroups.front();
    const auto drizzleOf = [&manifest](const WorkflowManifest::IntegrationGroup& stated) {
        return integrationGroupFingerprintInput(manifest, stated).value(QStringLiteral("drizzle")).toObject();
    };

    group.stacking.normalizationMode = NormalizationMode::Local;
    group.stacking.localNormalization.enabled = true;
    EXPECT_TRUE(drizzleOf(group).isEmpty()) << "a group that does not drizzle";

    group.drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    group.drizzle->enabled = true;
    const QJsonObject drizzledLocal = drizzleOf(group);
    EXPECT_EQ(drizzledLocal.value(QStringLiteral("localNormalizationRevision")).toInt(),
              ImageIntegration::schema::kDrizzledLocalNormalizationRevision);
    EXPECT_EQ(drizzledLocal.value(QStringLiteral("levelRevision")).toInt(),
              ImageIntegration::schema::kDrizzledMasterLevelRevision);
    EXPECT_EQ(drizzledLocal.value(QStringLiteral("solutionRevision")).toInt(),
              ImageIntegration::schema::kDrizzledMasterSolutionRevision);

    group.stacking.localNormalization.enabled = false;
    EXPECT_FALSE(drizzleOf(group).contains(QStringLiteral("localNormalizationRevision")))
        << "a drizzled group whose local normalization is off";
    group.stacking.localNormalization.enabled = true;
    group.stacking.normalizationMode = NormalizationMode::Global;
    EXPECT_FALSE(drizzleOf(group).contains(QStringLiteral("localNormalizationRevision"))) << "a drizzled Global group";
    group.stacking.normalizationMode = NormalizationMode::Local;
    group.drizzle->enabled = false;
    EXPECT_FALSE(drizzleOf(group).contains(QStringLiteral("localNormalizationRevision")))
        << "a Local group whose drizzle is off";
}

TEST(ManifestHashing, WorkflowRawLoadPolicyFingerprintDocumentsNeutralRawCalibration)
{
    const QJsonObject policy = ImageIntegration::workflowRawLoadPolicyHashObject();

    EXPECT_EQ(policy.value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::kWorkflowRawLoadPolicyHashVersion);
    EXPECT_EQ(policy.value(QStringLiteral("cfaWhiteBalance")).toInt(-1),
              static_cast<int>(an::CfaWhiteBalanceMode::None));
    EXPECT_EQ(policy.value(QStringLiteral("fullColorWhiteBalance")).toInt(-1),
              static_cast<int>(an::RawFullColorWhiteBalance::None));
    EXPECT_EQ(policy.value(QStringLiteral("calibrationDomain")).toString(), QStringLiteral("raw-before-demosaic"));
    EXPECT_EQ(ImageIntegration::workflowRawWhiteBalanceModeName(), QStringLiteral("none"));

    const ImageIntegration::WorkflowRawPolicy run_policy = ImageIntegration::workflowRawPolicy();
    EXPECT_EQ(run_policy.cfaWhiteBalance, an::CfaWhiteBalanceMode::None);
    EXPECT_EQ(run_policy.fullColorWhiteBalance, an::RawFullColorWhiteBalance::None);
    EXPECT_EQ(ImageIntegration::workflowRawCodecOptions(run_policy).fullColorWhiteBalance,
              an::RawFullColorWhiteBalance::None);
}

TEST(WorkflowManifest, IntegrationGroupRoundTripsRejectionProvenance)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("auto_rej_a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("auto_rej_b.fit"));

    WorkflowManifest manifest = makeManifestWithFrames({frame_a, frame_b});
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);

    auto& group = manifest.integrationGroups.front();
    group.stacking.requestedRejectionMethod = QStringLiteral("Auto");
    group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.integrationGroups.size(), 1u);
    EXPECT_EQ(round_trip.integrationGroups.front().stacking.requestedRejectionMethod, QStringLiteral("Auto"));
    EXPECT_EQ(round_trip.integrationGroups.front().stacking.rejectionMethod, QStringLiteral("Sigma Clipping"));
}

TEST(WorkflowManifest, IntegrationGroupRoundTripsHdrExposureMergeSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("hdr_a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("hdr_b.fit"));

    WorkflowManifest manifest = makeManifestWithFrames({frame_a, frame_b});
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);

    auto& group = manifest.integrationGroups.front();
    group.stacking.hdr.enabled = true;

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.integrationGroups.size(), 1u);
    EXPECT_TRUE(round_trip.integrationGroups.front().stacking.hdr.enabled);
}

TEST(WorkflowManifest, IntegrationGroupRoundTripsFinalMasterFallbackSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("wcs_a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("wcs_b.fit"));

    WorkflowManifest manifest = makeManifestWithFrames({frame_a, frame_b});
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);

    auto& group = manifest.integrationGroups.front();
    group.stacking.solveFinalMasterIfNeeded = false;

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.integrationGroups.size(), 1u);
    EXPECT_FALSE(round_trip.integrationGroups.front().stacking.solveFinalMasterIfNeeded);
}

TEST(WorkflowManifest, IntegrationGroupRoundTripsOverlapNormalizationSetting)
{
    FrameId frame_a(QDir::tempPath() + QDir::separator() + QStringLiteral("ov_a.fit"));
    FrameId frame_b(QDir::tempPath() + QDir::separator() + QStringLiteral("ov_b.fit"));

    WorkflowManifest manifest = makeManifestWithFrames({frame_a, frame_b});
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);

    auto& group = manifest.integrationGroups.front();
    group.stacking.normalizationMode = NormalizationMode::Global;
    group.stacking.overlapNormalization.enabled = true;
    group.stacking.overlapNormalization.minOverlapPixels = 1536;
    group.stacking.overlapNormalization.minOverlapFraction = 0.03;

    const WorkflowManifest round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.integrationGroups.size(), 1u);

    const auto& overlap = round_trip.integrationGroups.front().stacking.overlapNormalization;
    EXPECT_TRUE(overlap.enabled);
    EXPECT_EQ(overlap.minOverlapPixels, 1536);
    EXPECT_DOUBLE_EQ(overlap.minOverlapFraction, 0.03);
}

TEST(WorkflowManifestRegisteredOutput, RegisteredOutputPolicyDefaultsToRetainedRoundTripsAndChangesHash)
{
    WorkflowManifest manifest;
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|registered-output|L")}};
    group.outputFilename = QStringLiteral("registered_output_master.fit");
    group.lightFiles = {FrameId(QStringLiteral("/tmp/registered_output_a.fit")),
                        FrameId(QStringLiteral("/tmp/registered_output_b.fit"))};
    group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    manifest.integrationGroups.push_back(group);

    const QString retained_hash = computeStackingSettingsHash(StackingSettings{});
    StackingSettings discardable;
    discardable.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    EXPECT_NE(retained_hash, computeStackingSettingsHash(discardable));

    const auto round_trip = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(round_trip.integrationGroups.size(), 1u);
    EXPECT_EQ(round_trip.integrationGroups.front().stacking.registeredOutputPolicy,
              RegisteredOutputPolicy::Discardable);

    QJsonObject unstated_json = manifest.toJson();
    QJsonArray integration_groups = unstated_json.value(QStringLiteral("integrationGroups")).toArray();
    ASSERT_EQ(integration_groups.size(), 1);
    QJsonObject unstated_group = integration_groups.at(0).toObject();
    QJsonObject unstated_stacking = unstated_group.value(QStringLiteral("stacking")).toObject();
    unstated_stacking.remove(QStringLiteral("registeredOutputPolicy"));
    unstated_group.insert(QStringLiteral("stacking"), unstated_stacking);
    integration_groups[0] = unstated_group;
    unstated_json.insert(QStringLiteral("integrationGroups"), integration_groups);

    const auto unstated = WorkflowManifest::fromJson(unstated_json);
    ASSERT_EQ(unstated.integrationGroups.size(), 1u);
    EXPECT_EQ(unstated.integrationGroups.front().stacking.registeredOutputPolicy, RegisteredOutputPolicy::Retained);
}

TEST(ManifestDiff, DetectsRegisteredOutputPolicyChange)
{
    WorkflowManifest previous = makeManifestWithFrames({FrameId(QStringLiteral("/tmp/registered_output_diff_a.fit")),
                                                        FrameId(QStringLiteral("/tmp/registered_output_diff_b.fit"))});
    WorkflowManifest current = previous;
    current.integrationGroups.front().stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;

    const ManifestDiff diff = compareManifests(previous, current);

    ASSERT_EQ(diff.integrationChanges.size(), 1u);
    EXPECT_EQ(diff.integrationChanges.front().change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_EQ(diff.integrationChanges.front().settingsChanged.stages(),
              (LightStages{LightStage::Alignment, LightStage::Stacking}));
    EXPECT_EQ(diff.integrationChanges.front().settingsChanged.alignment,
              QStringList{QStringLiteral("Registered-output policy changed")});
}

// Every field of the stacking settings concerns the stages its entry names, and a change of it those stages alone. A
// field added to the settings stops the build until it has its entry, and this case until it has its line here, which
// says again which stages read it.
TEST(ManifestDiff, EveryStackingSettingConcernsTheStagesThatReadIt)
{
    const LightStages stack{LightStage::Stacking};
    const LightStages normalized{LightStage::Normalization, LightStage::Stacking};
    const LightStages drizzled{LightStage::Drizzle, LightStage::Stacking};
    const LightStages drizzlePlane{LightStage::Normalization, LightStage::Drizzle, LightStage::Stacking};
    const LightStages aligned{LightStage::Alignment, LightStage::Stacking};
    const LightStages every{LightStage::Alignment, LightStage::Normalization, LightStage::Drizzle,
                            LightStage::Stacking};

    struct Expected
    {
        const char* name;
        std::function<void(StackingSettings&)> change;
        LightStages stages;
    };
    const std::vector<Expected> expected = {
        {"stackMethod", [](StackingSettings& s) { s.stackMethod = QStringLiteral("Median"); }, stack},
        {"rejectionMethod", [](StackingSettings& s) { s.rejectionMethod = QStringLiteral("None"); }, stack},
        {"requestedRejectionMethod", [](StackingSettings& s) { s.requestedRejectionMethod = QStringLiteral("Auto"); },
         LightStages{}},
        {"sigmaLow", [](StackingSettings& s) { s.sigmaLow += 0.5; }, stack},
        {"sigmaHigh", [](StackingSettings& s) { s.sigmaHigh += 0.5; }, stack},
        {"weighting", [](StackingSettings& s) { s.weighting = QStringLiteral("Noise"); }, stack},
        {"createQualityMaps", [](StackingSettings& s) { s.createQualityMaps = !s.createQualityMaps; }, stack},
        {"createRejectionMaps", [](StackingSettings& s) { s.createRejectionMaps = !s.createRejectionMaps; }, stack},
        {"mergeRejectionMaps", [](StackingSettings& s) { s.mergeRejectionMaps = !s.mergeRejectionMaps; }, stack},
        {"spatialRejectionClustering",
         [](StackingSettings& s) { s.spatialRejectionClustering = !s.spatialRejectionClustering; }, stack},
        {"restackExcludingFlaggedFrames",
         [](StackingSettings& s) { s.restackExcludingFlaggedFrames = !s.restackExcludingFlaggedFrames; }, stack},
        {"starCountScaling", [](StackingSettings& s) { s.starCountScaling = QStringLiteral("Linear"); }, stack},
        {"exponentialAlpha", [](StackingSettings& s) { s.exponentialAlpha += 0.5; }, stack},
        {"minWeightFloor", [](StackingSettings& s) { s.minWeightFloor += 0.5; }, stack},
        {"localWeighting.enabled", [](StackingSettings& s) { s.localWeighting.enabled = !s.localWeighting.enabled; },
         stack},
        {"localWeighting.windowSize", [](StackingSettings& s) { s.localWeighting.windowSize += 2; }, stack},
        {"localWeighting.gridSpacing", [](StackingSettings& s) { s.localWeighting.gridSpacing += 2; }, stack},
        {"localWeighting.snrWeightExponent", [](StackingSettings& s) { s.localWeighting.snrWeightExponent += 0.5; },
         stack},
        {"limitMemory", [](StackingSettings& s) { s.limitMemory = !s.limitMemory; }, stack},
        {"maxMemoryMb", [](StackingSettings& s) { s.maxMemoryMb += 1; }, stack},
        {"normalizationMode", [](StackingSettings& s) { s.normalizationMode = NormalizationMode::Local; }, normalized},
        {"globalNormalizationMethod",
         [](StackingSettings& s) { s.globalNormalizationMethod = GlobalNormalizationMethod::Additive; }, normalized},
        {"scaleEstimator", [](StackingSettings& s) { s.scaleEstimator = ScaleEstimatorMethod::MAD; }, normalized},
        {"overlapNormalization.enabled",
         [](StackingSettings& s) { s.overlapNormalization.enabled = !s.overlapNormalization.enabled; }, normalized},
        {"overlapNormalization.minOverlapPixels",
         [](StackingSettings& s) { s.overlapNormalization.minOverlapPixels += 1; }, normalized},
        {"overlapNormalization.minOverlapFraction",
         [](StackingSettings& s) { s.overlapNormalization.minOverlapFraction += 0.5; }, normalized},
        {"normalizationReferenceMode",
         [](StackingSettings& s) { s.normalizationReferenceMode = NormalizationReferenceMode::Manual; }, normalized},
        {"manualNormalizationReference",
         [](StackingSettings& s) { s.manualNormalizationReference = QStringLiteral("/tmp/reference.fit"); },
         normalized},
        {"localNormalization.enabled",
         [](StackingSettings& s) { s.localNormalization.enabled = !s.localNormalization.enabled; }, normalized},
        {"localNormalization.gridSize", [](StackingSettings& s) { s.localNormalization.gridSize += 1; }, normalized},
        {"localNormalization.gridInterpolation",
         [](StackingSettings& s) { s.localNormalization.gridInterpolation = QStringLiteral("Bilinear"); }, normalized},
        {"localNormalization.localScaleEnabled",
         [](StackingSettings& s) { s.localNormalization.localScaleEnabled = !s.localNormalization.localScaleEnabled; },
         normalized},
        {"localNormalization.referenceStrategy",
         [](StackingSettings& s) { s.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame"); },
         normalized},
        {"localNormalization.maxIntegratedFrames",
         [](StackingSettings& s) { s.localNormalization.maxIntegratedFrames += 1; }, normalized},
        {"localNormalization.evaluationCriteria",
         [](StackingSettings& s) { s.localNormalization.evaluationCriteria = QStringLiteral("PSF SNR"); }, normalized},
        {"localNormalization.backgroundEstimator",
         [](StackingSettings& s) { s.localNormalization.backgroundEstimator = QStringLiteral("Median"); }, normalized},
        {"localNormalization.backgroundPercentile",
         [](StackingSettings& s) { s.localNormalization.backgroundPercentile += 0.5; }, normalized},
        {"localNormalization.sigmaClipK", [](StackingSettings& s) { s.localNormalization.sigmaClipK += 0.5; },
         normalized},
        {"localNormalization.sigmaClipIterations",
         [](StackingSettings& s) { s.localNormalization.sigmaClipIterations += 1; }, normalized},
        {"localNormalization.backgroundSmoothingRadius",
         [](StackingSettings& s) { s.localNormalization.backgroundSmoothingRadius += 1; }, normalized},
        {"localNormalization.scaleEvaluation",
         [](StackingSettings& s) { s.localNormalization.scaleEvaluation = QStringLiteral("Median"); }, normalized},
        {"localNormalization.scaleMode",
         [](StackingSettings& s) { s.localNormalization.scaleMode = QStringLiteral("Independent"); }, normalized},
        {"localNormalization.psfType",
         [](StackingSettings& s) { s.localNormalization.psfType = QStringLiteral("Gaussian"); }, normalized},
        {"localNormalization.moffatBeta", [](StackingSettings& s) { s.localNormalization.moffatBeta += 0.5; },
         normalized},
        {"localNormalization.maxStars", [](StackingSettings& s) { s.localNormalization.maxStars += 1; }, normalized},
        {"localNormalization.minDetectionSnr", [](StackingSettings& s) { s.localNormalization.minDetectionSnr += 0.5; },
         normalized},
        {"localNormalization.allowClusteredSources",
         [](StackingSettings& s) {
             s.localNormalization.allowClusteredSources = !s.localNormalization.allowClusteredSources;
         },
         normalized},
        {"localNormalization.lowClip", [](StackingSettings& s) { s.localNormalization.lowClip += 0.5; }, normalized},
        {"localNormalization.highClip", [](StackingSettings& s) { s.localNormalization.highClip -= 0.5; }, normalized},
        {"localNormalization.hotPixelMedianRadius",
         [](StackingSettings& s) { s.localNormalization.hotPixelMedianRadius += 1; }, normalized},
        {"localNormalization.noiseReductionRadius",
         [](StackingSettings& s) { s.localNormalization.noiseReductionRadius += 1; }, normalized},
        {"localNormalization.localScaleSmoothingRadius",
         [](StackingSettings& s) { s.localNormalization.localScaleSmoothingRadius += 1; }, normalized},
        {"drizzle.enabled", [](StackingSettings& s) { s.drizzle.enabled = !s.drizzle.enabled; }, drizzlePlane},
        {"drizzle.scale", [](StackingSettings& s) { s.drizzle.scale += 1.0; }, drizzled},
        {"drizzle.pixelFraction", [](StackingSettings& s) { s.drizzle.pixelFraction -= 0.5; }, drizzled},
        {"drizzle.kernel", [](StackingSettings& s) { s.drizzle.kernel = QStringLiteral("Gaussian"); }, drizzled},
        {"drizzle.cfaAware", [](StackingSettings& s) { s.drizzle.cfaAware = !s.drizzle.cfaAware; }, drizzled},
        {"drizzle.cfaPattern", [](StackingSettings& s) { s.drizzle.cfaPattern = QStringLiteral("BGGR"); }, drizzled},
        {"cometStackingMode", [](StackingSettings& s) { s.cometStackingMode = QStringLiteral("Comet"); }, every},
        {"solveFinalMasterIfNeeded",
         [](StackingSettings& s) { s.solveFinalMasterIfNeeded = !s.solveFinalMasterIfNeeded; }, stack},
        {"registeredOutputPolicy",
         [](StackingSettings& s) { s.registeredOutputPolicy = RegisteredOutputPolicy::Discardable; }, aligned},
        {"hdr.enabled", [](StackingSettings& s) { s.hdr.enabled = !s.hdr.enabled; }, stack},
    };

    const std::span<const StackingSettingReaders> readers = stackingSettingReaders();
    ASSERT_EQ(readers.size(), expected.size());
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        SCOPED_TRACE(expected[index].name);
        EXPECT_STREQ(readers[index].name, expected[index].name);
        EXPECT_EQ(readers[index].stages, expected[index].stages);

        const StackingSettings previous;
        StackingSettings current = previous;
        expected[index].change(current);
        EXPECT_FALSE(readers[index].same(previous, current));
        for (std::size_t other = 0u; other < readers.size(); ++other)
        {
            if (other != index)
            {
                EXPECT_TRUE(readers[other].same(previous, current)) << readers[other].name << " reads the field too";
            }
        }
        EXPECT_EQ(changedStackingSettings(previous, current).stages(), expected[index].stages);
    }
}

// A change of the stacking method concerns the stack alone, not the alignment and the normalization it reads; a
// change of the normalization's reference frames, the normalization and the stack.
TEST(ManifestDiff, AChangedStackingMethodConcernsTheStackAlone)
{
    const std::vector<FrameId> lights = {
        FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("method_a.fit")),
        FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("method_b.fit"))};
    const WorkflowManifest previous = makeManifestWithFrames(lights);
    WorkflowManifest current = previous;
    current.integrationGroups.front().stacking.stackMethod = QStringLiteral("Median");

    const ManifestDiff method = compareManifests(previous, current);
    ASSERT_EQ(method.integrationChanges.size(), 1u);
    const auto& change = method.integrationChanges.front();
    EXPECT_EQ(change.change, ManifestDiff::CalibrationGroupChange::ChangeType::Modified);
    EXPECT_FALSE(change.lightFilesChanged);
    EXPECT_FALSE(change.calibrationAssignmentsChanged);
    EXPECT_EQ(change.settingsChanged.stacking, QStringList{QStringLiteral("Stacking settings changed")});
    EXPECT_TRUE(change.settingsChanged.alignment.isEmpty());
    EXPECT_TRUE(change.settingsChanged.normalization.isEmpty());
    EXPECT_TRUE(change.settingsChanged.drizzle.isEmpty());

    WorkflowManifest references = previous;
    references.integrationGroups.front().normalizationReferenceFrames = {lights.back()};
    const ManifestDiff reference = compareManifests(previous, references);
    ASSERT_EQ(reference.integrationChanges.size(), 1u);
    EXPECT_EQ(reference.integrationChanges.front().settingsChanged.stages(),
              (LightStages{LightStage::Normalization, LightStage::Stacking}));
    EXPECT_EQ(reference.integrationChanges.front().settingsChanged.normalization,
              QStringList{QStringLiteral("Normalization reference frames changed")});
}

// ---- The lights the user left out: named by the manifest, read by nothing a run decides.

namespace
{
/// The entry of a manifest's left-out lights that names a light; null when none does.
const WorkflowManifest::LeftOutLight* leftOutEntry(const WorkflowManifest& manifest, const FrameId& light)
{
    const auto found =
        std::find_if(manifest.leftOutLights.begin(), manifest.leftOutLights.end(),
                     [&light](const WorkflowManifest::LeftOutLight& entry) { return entry.light == light; });
    return found == manifest.leftOutLights.end() ? nullptr : &*found;
}

bool holdsLight(const std::vector<FrameId>& lights, const FrameId& light)
{
    return std::find(lights.begin(), lights.end(), light) != lights.end();
}

FrameInfo leftOutTestLight(const QString& name, const QString& filter)
{
    return makeFrame(FrameType::Light, name, QSize(100, 100), 60.0, filter, QStringLiteral("M42"),
                     QStringLiteral("2025-01-01"));
}
} // namespace

TEST(WorkflowManifest, TheLightsLeftOutRoundTrip)
{
    const FrameId kept(QDir::tempPath() + QDir::separator() + QStringLiteral("left_out_kept.fit"));
    const FrameId excluded(QDir::tempPath() + QDir::separator() + QStringLiteral("left_out_excluded.fit"));
    const FrameId fromStack(QDir::tempPath() + QDir::separator() + QStringLiteral("left_out_from_stack.fit"));
    WorkflowManifest manifest = makeManifestWithFrames({kept});
    EXPECT_FALSE(manifest.toJson().contains(QStringLiteral("leftOutLights")))
        << "a manifest that leaves no light out states no list";

    manifest.leftOutLights = {
        WorkflowManifest::LeftOutLight{.light = excluded,
                                       .kind = WorkflowManifest::LeftOutLight::Kind::Excluded,
                                       .groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|M42|Ha")}}},
        WorkflowManifest::LeftOutLight{.light = fromStack,
                                       .kind = WorkflowManifest::LeftOutLight::Kind::StackExcluded,
                                       .groupKey = manifest.integrationGroups.front().groupKey}};

    const QJsonObject json = manifest.toJson();
    ASSERT_EQ(json.value(QStringLiteral("leftOutLights")).toArray().size(), 2);
    const WorkflowManifest parsed = WorkflowManifest::fromJson(json);

    ASSERT_EQ(parsed.leftOutLights.size(), 2u);
    EXPECT_EQ(parsed.leftOutLights[0].light, excluded);
    EXPECT_EQ(parsed.leftOutLights[0].kind, WorkflowManifest::LeftOutLight::Kind::Excluded);
    EXPECT_EQ(parsed.leftOutLights[0].groupKey.toString(), QStringLiteral("int|M42|Ha"));
    EXPECT_EQ(parsed.leftOutLights[1].light, fromStack);
    EXPECT_EQ(parsed.leftOutLights[1].kind, WorkflowManifest::LeftOutLight::Kind::StackExcluded);
    EXPECT_TRUE(parsed.leftOutLights[1].groupKey == manifest.integrationGroups.front().groupKey);
    EXPECT_EQ(QJsonDocument(parsed.toJson()).toJson(QJsonDocument::Compact),
              QJsonDocument(json).toJson(QJsonDocument::Compact));
    EXPECT_EQ(manifest.redactedForPersistence().leftOutLights.size(), 2u);
}

TEST(ManifestHashing, TheLightsLeftOutTakeNoPartInAnyHash)
{
    const FrameId light_a(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_left_out_a.fit"));
    const FrameId light_b(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_left_out_b.fit"));
    const FrameId bias(QDir::tempPath() + QDir::separator() + QStringLiteral("hash_left_out_bias.fit"));
    WorkflowManifest without = makeManifestWithFrames({light_a, light_b});
    WorkflowManifest::CalibrationStage bias_stage;
    bias_stage.frameType = QStringLiteral("Bias");
    WorkflowManifest::CalibrationStage::Group bias_group;
    bias_group.groupKey = GroupKey{QStringLiteral("bias|100x100")};
    bias_group.outputFilename = QStringLiteral("master_bias.fit");
    bias_group.inputFiles = {bias};
    bias_stage.groups.push_back(bias_group);
    without.biasStage = bias_stage;
    for (const FrameId& light : {light_a, light_b})
    {
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = light;
        params.biasPath = FrameId(QStringLiteral("D:/Output/master_bias.fit"));
        without.lightsCalibration.push_back(params);
    }

    WorkflowManifest with = without;
    with.leftOutLights = {WorkflowManifest::LeftOutLight{.light = FrameId(QDir::tempPath() + QDir::separator() +
                                                                          QStringLiteral("hash_left_out_excluded.fit")),
                                                         .kind = WorkflowManifest::LeftOutLight::Kind::Excluded,
                                                         .groupKey = with.integrationGroups.front().groupKey},
                          WorkflowManifest::LeftOutLight{.light = FrameId(QDir::tempPath() + QDir::separator() +
                                                                          QStringLiteral("hash_left_out_stack.fit")),
                                                         .kind = WorkflowManifest::LeftOutLight::Kind::StackExcluded,
                                                         .groupKey = with.integrationGroups.front().groupKey}};

    EXPECT_EQ(computeIntegrationGroupHash(with, with.integrationGroups.front()),
              computeIntegrationGroupHash(without, without.integrationGroups.front()));
    EXPECT_EQ(integrationGroupFingerprintInput(with, with.integrationGroups.front()),
              integrationGroupFingerprintInput(without, without.integrationGroups.front()));
    EXPECT_EQ(computeCalibrationGroupHash(with, with.biasStage->groups.front()),
              computeCalibrationGroupHash(without, without.biasStage->groups.front()));
    EXPECT_EQ(
        computeLightsCalibrationHashForSubset(with.lightsCalibration, with.integrationGroups.front().lightFiles),
        computeLightsCalibrationHashForSubset(without.lightsCalibration, without.integrationGroups.front().lightFiles));
    EXPECT_EQ(registrationGroupMaterializationContractFingerprint(with, with.registrationGroups.front()),
              registrationGroupMaterializationContractFingerprint(without, without.registrationGroups.front()));
}

TEST(ManifestDiff, TheLightsLeftOutAloneAreNoChange)
{
    const FrameId light_a(QDir::tempPath() + QDir::separator() + QStringLiteral("diff_left_out_a.fit"));
    const FrameId light_b(QDir::tempPath() + QDir::separator() + QStringLiteral("diff_left_out_b.fit"));
    const WorkflowManifest previous = makeManifestWithFrames({light_a, light_b});
    WorkflowManifest current = previous;
    current.leftOutLights = {WorkflowManifest::LeftOutLight{
        .light = FrameId(QDir::tempPath() + QDir::separator() + QStringLiteral("diff_left_out_excluded.fit")),
        .kind = WorkflowManifest::LeftOutLight::Kind::Excluded,
        .groupKey = current.integrationGroups.front().groupKey}};

    for (const ManifestDiff& diff : {compareManifests(previous, current), compareManifests(current, previous)})
    {
        // Each group is listed as unchanged: the diff states no addition, removal or modification.
        EXPECT_FALSE(diff.hasChanges()) << diff.toSummaryText().toStdString();
        EXPECT_EQ(diff.totalModifications(), 0);
        EXPECT_TRUE(diff.lightsCalibrationChanges.empty());
    }
}

TEST(ManifestBuilder, ALightLeftOutIsNamedOnceUnderTheGroupItWouldHaveJoined)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    const FrameInfo keep_a = leftOutTestLight(QStringLiteral("left_out_keep_a.fit"), QStringLiteral("L"));
    const FrameInfo keep_b = leftOutTestLight(QStringLiteral("left_out_keep_b.fit"), QStringLiteral("L"));
    const FrameInfo excluded = leftOutTestLight(QStringLiteral("left_out_excluded.fit"), QStringLiteral("L"));
    const FrameInfo from_stack = leftOutTestLight(QStringLiteral("left_out_from_stack.fit"), QStringLiteral("L"));
    model.addFrames(FrameType::Light, {keep_a, keep_b, excluded, from_stack});
    model.setFramesExcluded(FrameType::Light, {excluded.id.canonicalPath()}, true);
    ASSERT_EQ(model.integrationGroups().size(), 1u);
    model.setFramesStackExcluded(model.integrationGroups().front(), {from_stack.id.canonicalPath()}, true);

    const WorkflowManifest manifest = ManifestBuilder(model).build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    const WorkflowManifest::IntegrationGroup& group = manifest.integrationGroups.front();
    ASSERT_EQ(manifest.leftOutLights.size(), 2u) << "each light left out appears once";

    const WorkflowManifest::LeftOutLight* excluded_entry = leftOutEntry(manifest, excluded.id);
    ASSERT_NE(excluded_entry, nullptr);
    EXPECT_EQ(excluded_entry->kind, WorkflowManifest::LeftOutLight::Kind::Excluded);
    EXPECT_TRUE(excluded_entry->groupKey == group.groupKey) << "the group the grouping would have put it in";

    const WorkflowManifest::LeftOutLight* stack_entry = leftOutEntry(manifest, from_stack.id);
    ASSERT_NE(stack_entry, nullptr);
    EXPECT_EQ(stack_entry->kind, WorkflowManifest::LeftOutLight::Kind::StackExcluded);
    EXPECT_TRUE(stack_entry->groupKey == group.groupKey);

    // Neither is a light of the integration group; the light left out of the stack alone is still registered.
    EXPECT_EQ(group.lightFiles.size(), 2u);
    EXPECT_FALSE(holdsLight(group.lightFiles, excluded.id));
    EXPECT_FALSE(holdsLight(group.lightFiles, from_stack.id));
    ASSERT_EQ(manifest.registrationGroups.size(), 1u);
    EXPECT_TRUE(holdsLight(manifest.registrationGroups.front().lightFiles, from_stack.id));
    EXPECT_FALSE(holdsLight(manifest.registrationGroups.front().lightFiles, excluded.id));

    // The list is the same however often the manifest is built.
    const WorkflowManifest again = ManifestBuilder(model).build();
    EXPECT_EQ(QJsonDocument(again.toJson()).toJson(QJsonDocument::Compact),
              QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact));
}

TEST(ManifestBuilder, AGroupWhoseEveryLightIsLeftOutYieldsNoGroupAndListsItsLights)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    const FrameInfo lum_a = leftOutTestLight(QStringLiteral("whole_group_lum_a.fit"), QStringLiteral("L"));
    const FrameInfo lum_b = leftOutTestLight(QStringLiteral("whole_group_lum_b.fit"), QStringLiteral("L"));
    const FrameInfo ha_a = leftOutTestLight(QStringLiteral("whole_group_ha_a.fit"), QStringLiteral("Ha"));
    const FrameInfo ha_b = leftOutTestLight(QStringLiteral("whole_group_ha_b.fit"), QStringLiteral("Ha"));
    const FrameInfo oiii_a = leftOutTestLight(QStringLiteral("whole_group_oiii_a.fit"), QStringLiteral("OIII"));
    const FrameInfo oiii_b = leftOutTestLight(QStringLiteral("whole_group_oiii_b.fit"), QStringLiteral("OIII"));
    model.addFrames(FrameType::Light, {lum_a, lum_b, ha_a, ha_b, oiii_a, oiii_b});

    // Every hydrogen-alpha light is excluded in the lights' list; every oxygen light from its group's stack.
    const IntegrationGroupKey ha_key =
        IntegrationGroupEngine::groupKeyFor(ha_a, model.lightIntegrationGroupingOptions());
    const IntegrationGroupKey oiii_key =
        IntegrationGroupEngine::groupKeyFor(oiii_a, model.lightIntegrationGroupingOptions());
    model.setFramesExcluded(FrameType::Light, {ha_a.id.canonicalPath(), ha_b.id.canonicalPath()}, true);
    model.setFramesStackExcluded(oiii_key.key, {oiii_a.id.canonicalPath(), oiii_b.id.canonicalPath()}, true);

    const WorkflowManifest manifest = ManifestBuilder(model).build();

    ASSERT_EQ(manifest.integrationGroups.size(), 1u) << "a group with no light to stack is not made";
    EXPECT_FALSE(manifest.integrationGroups.front().groupKey == ha_key);
    EXPECT_FALSE(manifest.integrationGroups.front().groupKey == oiii_key);
    ASSERT_EQ(manifest.leftOutLights.size(), 4u);
    for (const FrameInfo* light : {&ha_a, &ha_b})
    {
        const WorkflowManifest::LeftOutLight* entry = leftOutEntry(manifest, light->id);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->kind, WorkflowManifest::LeftOutLight::Kind::Excluded);
        EXPECT_TRUE(entry->groupKey == ha_key) << entry->groupKey.toString().toStdString();
    }
    for (const FrameInfo* light : {&oiii_a, &oiii_b})
    {
        const WorkflowManifest::LeftOutLight* entry = leftOutEntry(manifest, light->id);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->kind, WorkflowManifest::LeftOutLight::Kind::StackExcluded);
        EXPECT_TRUE(entry->groupKey == oiii_key) << entry->groupKey.toString().toStdString();
    }
}

TEST(ManifestBuilder, TheLightsLeftOutChangeNothingElseAManifestStates)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    const FrameInfo keep_a = leftOutTestLight(QStringLiteral("nothing_else_keep_a.fit"), QStringLiteral("L"));
    const FrameInfo keep_b = leftOutTestLight(QStringLiteral("nothing_else_keep_b.fit"), QStringLiteral("L"));
    const FrameInfo excluded = leftOutTestLight(QStringLiteral("nothing_else_excluded.fit"), QStringLiteral("L"));
    const FrameInfo from_stack = leftOutTestLight(QStringLiteral("nothing_else_from_stack.fit"), QStringLiteral("L"));
    model.addFrames(FrameType::Bias,
                    {makeFrame(FrameType::Bias, QStringLiteral("nothing_else_bias.fit"), QSize(100, 100), 0.0,
                               QString(), QString(), QStringLiteral("2025-01-01"))});
    model.addFrames(FrameType::Light, {keep_a, keep_b, excluded, from_stack});
    model.setFramesExcluded(FrameType::Light, {excluded.id.canonicalPath()}, true);
    ASSERT_EQ(model.integrationGroups().size(), 1u);
    model.setFramesStackExcluded(model.integrationGroups().front(), {from_stack.id.canonicalPath()}, true);

    const WorkflowManifest listed = ManifestBuilder(model).build();
    ASSERT_EQ(listed.leftOutLights.size(), 2u);
    WorkflowManifest emptied = listed;
    emptied.leftOutLights.clear();

    // The document differs by the one key.
    QJsonObject listed_json = listed.toJson();
    ASSERT_TRUE(listed_json.contains(QStringLiteral("leftOutLights")));
    listed_json.remove(QStringLiteral("leftOutLights"));
    EXPECT_EQ(QJsonDocument(listed_json).toJson(QJsonDocument::Compact),
              QJsonDocument(emptied.toJson()).toJson(QJsonDocument::Compact));

    // And nothing a run decides by differs.
    ASSERT_EQ(listed.integrationGroups.size(), 1u);
    EXPECT_EQ(computeIntegrationGroupHash(listed, listed.integrationGroups.front()),
              computeIntegrationGroupHash(emptied, emptied.integrationGroups.front()));
    ASSERT_TRUE(listed.biasStage.has_value());
    EXPECT_EQ(computeCalibrationGroupHash(listed, listed.biasStage->groups.front()),
              computeCalibrationGroupHash(emptied, emptied.biasStage->groups.front()));
    const ManifestDiff diff = compareManifests(emptied, listed);
    EXPECT_FALSE(diff.hasChanges()) << diff.toSummaryText().toStdString();
    EXPECT_EQ(diff.totalModifications(), 0);
}

TEST(ManifestBuilder, ARunOfOneGroupListsOnlyTheLightsLeftOutOfThatGroup)
{
    IntegrationDataModel model;
    model.setOutputDirectory(QDir::tempPath());
    const FrameInfo lum_a = leftOutTestLight(QStringLiteral("one_group_lum_a.fit"), QStringLiteral("L"));
    const FrameInfo lum_b = leftOutTestLight(QStringLiteral("one_group_lum_b.fit"), QStringLiteral("L"));
    const FrameInfo lum_out = leftOutTestLight(QStringLiteral("one_group_lum_out.fit"), QStringLiteral("L"));
    const FrameInfo ha_a = leftOutTestLight(QStringLiteral("one_group_ha_a.fit"), QStringLiteral("Ha"));
    const FrameInfo ha_b = leftOutTestLight(QStringLiteral("one_group_ha_b.fit"), QStringLiteral("Ha"));
    const FrameInfo ha_out = leftOutTestLight(QStringLiteral("one_group_ha_out.fit"), QStringLiteral("Ha"));
    model.addFrames(FrameType::Light, {lum_a, lum_b, lum_out, ha_a, ha_b, ha_out});
    model.setFramesExcluded(FrameType::Light, {lum_out.id.canonicalPath(), ha_out.id.canonicalPath()}, true);
    const IntegrationGroupKey lum_key =
        IntegrationGroupEngine::groupKeyFor(lum_a, model.lightIntegrationGroupingOptions());

    const WorkflowManifest whole = ManifestBuilder(model).build();
    EXPECT_EQ(whole.leftOutLights.size(), 2u);

    const WorkflowManifest one = ManifestBuilder(model).buildForIntegrationGroup(lum_key);
    ASSERT_EQ(one.integrationGroups.size(), 1u);
    ASSERT_EQ(one.leftOutLights.size(), 1u);
    EXPECT_EQ(one.leftOutLights.front().light, lum_out.id);
    EXPECT_TRUE(one.leftOutLights.front().groupKey == lum_key);
}
