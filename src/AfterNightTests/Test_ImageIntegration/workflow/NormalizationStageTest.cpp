// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights normalization and drizzle stages. Normalization measures each group's lights where its
/// stack reads them - as they are read without a registration, on the registration's plane when the registered lights
/// are current, warped onto that plane in memory when they are not, and on a drizzled group's analysis plane - and
/// publishes the matches as a document and its sidecar that a later run reuses, that a settings change, an older
/// sidecar or a corrupt payload makes it measure again, and that a newer sidecar leaves untouched unless the group is
/// reprocessed. Drizzle writes one descriptor per light carrying the engine's identity of its geometry, dense
/// coordinates beside it when the group keeps checkpoints, and the group sidecar, reused while they describe the
/// registration.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/DrizzleDescriptorDocument.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsDrizzleStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsNormalizationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureStar;
using an::tests::integration::CalibrationRun;
using an::tests::integration::exposureStartCard;
using an::tests::integration::movedStars;
using an::tests::integration::starCatalogue;
using an::tests::integration::tanSolutionCards;
using an::tests::integration::writeStarField;
using ImageIntegration::NormalizationCoefficientMode;
using ImageIntegration::NormalizationMeasurementPlane;

[[nodiscard]] QByteArray contentsOf(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void rewriteJson(const QString& path, const std::function<void(QJsonObject&)>& edit)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    edit(object);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(object).toJson());
}

[[nodiscard]] QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(contentsOf(path)).object();
}

/// Holds the process in a numeric locale whose decimal separator is a comma, as QApplication leaves a Linux host
/// configured with one, and restores the locale it found. Not active when the host installs none.
class CommaDecimalLocale
{
public:
    CommaDecimalLocale()
    {
        const char* const current = std::setlocale(LC_NUMERIC, nullptr);
        m_saved = current != nullptr ? current : "C";
        for (const char* candidate : {"es_AR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "de-DE", "es-AR"})
        {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr && std::localeconv()->decimal_point[0] == ',')
            {
                m_active = true;
                return;
            }
        }
        std::setlocale(LC_NUMERIC, m_saved.c_str());
    }

    ~CommaDecimalLocale()
    {
        std::setlocale(LC_NUMERIC, m_saved.c_str());
    }

    CommaDecimalLocale(const CommaDecimalLocale&) = delete;
    CommaDecimalLocale& operator=(const CommaDecimalLocale&) = delete;

    [[nodiscard]] bool active() const noexcept
    {
        return m_active;
    }

private:
    std::string m_saved;
    bool m_active = false;
};

/// A group's lights, registered or not, as the normalization and drizzle stages find them.
class NormalizationStageTestBase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        ASSERT_TRUE(QDir().mkpath(QDir(m_root.path()).filePath(QStringLiteral("lights"))));
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("normalization-run");
        m_manifest.calibrateLightsInMemory = false;
        m_integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|lights|Lum")}};
        m_integration.stacking.normalizationMode = NormalizationMode::Global;
    }

    /// Writes a grey Float32 light of `base + perColumn * x + amplitude * u`, the noise `u` fixed by the seed.
    QString writeFlatLight(const QString& name, double base, double perColumn, double amplitude, std::uint64_t seed)
    {
        an::tests::fixtures::FixtureImage image;
        image.name = name.toStdString();
        image.width = 128u;
        image.height = 96u;
        image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
        image.pattern = an::tests::fixtures::FixturePattern::GradientNoise;
        image.seed = seed;
        an::tests::fixtures::FixtureChannelLevels levels;
        levels.base = base;
        levels.perColumn = perColumn;
        levels.amplitude = amplitude;
        image.levels = {levels};
        std::string error;
        const auto samples = an::tests::fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        EXPECT_TRUE(
            an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    void useUnregisteredLights(const std::vector<QString>& paths)
    {
        m_integration.lightFiles.clear();
        for (const QString& path : paths)
        {
            m_integration.lightFiles.emplace_back(path);
        }
        m_manifest.registrationGroups.clear();
        m_manifest.integrationGroups = {m_integration};
    }

    /// Two lights of one star field, the second moved and twice as bright over a higher background, registered.
    void registerTranslatedPair(double dx, double dy)
    {
        m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_registration.settings.autoReferenceDuringRegistration = false;
        m_registration.settings.enrichWithGaiaCatalog = false;
        m_registration.settings.transformMode = QStringLiteral("Linear");
        m_registration.settings.transformation = QStringLiteral("Affine");
        m_integration.registrationGroupKey = m_registration.groupKey;

        const std::vector<FixtureStar> reference = starCatalogue(30u, 17u);
        m_referencePath = writeStarLight(QStringLiteral("light_001"), reference, 71u);
        m_lightPath = writeStarLight(
            QStringLiteral("light_002"),
            movedStars(reference, [dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; }), 72u);
        m_registration.lightFiles = {FrameId(m_referencePath), FrameId(m_lightPath)};
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
        m_integration.lightFiles = m_registration.lightFiles;
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};
        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    }

    QString writeStarLight(const QString& name, const std::vector<FixtureStar>& stars, std::uint64_t seed)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        std::string error;
        EXPECT_TRUE(writeStarField(path, stars, {exposureStartCard("2026-03-01T01:00:00")}, seed, error)) << error;
        return path;
    }

    /// A starless light whose header states a solution with a second-order distortion.
    QString writeStarLightWithCards(const QString& name, std::uint64_t seed)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        std::vector<an::tests::fixtures::FixtureCard> cards = tanSolutionCards(80.5, 60.5, 2.0e-5);
        cards.push_back(exposureStartCard("2026-03-01T01:00:00"));
        std::string error;
        EXPECT_TRUE(writeStarField(path, {}, cards, seed, error)) << error;
        return path;
    }

    void updateGroup(const std::function<void(WorkflowManifest::IntegrationGroup&)>& edit)
    {
        edit(m_integration);
        m_manifest.integrationGroups = {m_integration};
    }

    /// Calibrates a light in memory by a flat master of one sixteen-bit code, zero at one sample when `deadAt` names
    /// one - outside the central third the flat's level is read from, so the flat divides by one and the light is not
    /// a number there.
    void calibrateInMemoryByAFlat(const QString& light, std::uint32_t width, std::uint32_t height,
                                  std::optional<std::pair<std::uint32_t, std::uint32_t>> deadAt = std::nullopt)
    {
        an::tests::fixtures::AcquisitionFacts facts;
        facts.kind = "Flat Field";
        const QString name = QFileInfo(light).completeBaseName() + QStringLiteral("_flat");
        an::tests::fixtures::FixtureImage flat =
            an::tests::fixtures::acquisitionFrame(name.toStdString(), facts, width, height);
        flat.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 20000.0}};
        if (deadAt.has_value())
        {
            flat.overrides = {
                an::tests::fixtures::FixtureSampleOverride{.x = deadAt->first, .y = deadAt->second, .value = 0.0}};
        }
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("masters/") + name + QStringLiteral(".fits"));
        std::string error;
        ASSERT_TRUE(an::tests::fixtures::writeAcquisitionFrame(flat, std::filesystem::path(path.toStdWString()), error))
            << error;
        m_manifest.calibrateLightsInMemory = true;
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = FrameId(light);
        params.flatPath = FrameId(path);
        m_manifest.lightsCalibration.push_back(params);
    }

    StageResult normalize()
    {
        LightsNormalizationStage stage;
        return m_run.run(stage, m_manifest);
    }

    StageResult drizzle()
    {
        LightsDrizzleStage stage;
        return m_run.run(stage, m_manifest);
    }

    [[nodiscard]] QString artifactPath() const
    {
        return stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front());
    }

    [[nodiscard]] std::optional<ImageIntegration::NormalizationCoefficientSet> coefficients() const
    {
        const auto read = ImageIntegration::readNormalizationCoefficients(artifactPath(), std::nullopt);
        EXPECT_TRUE(read.coefficients.has_value()) << read.message.toStdString();
        return read.coefficients;
    }

    [[nodiscard]] QString sidecarPath() const
    {
        return ImageIntegration::normalizationSidecarPath(artifactPath());
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
    WorkflowManifest::RegistrationGroup m_registration;
    WorkflowManifest::IntegrationGroup m_integration;
    QString m_referencePath;
    QString m_lightPath;
};

class LightsNormalizationTest : public NormalizationStageTestBase
{
};

TEST_F(LightsNormalizationTest, AGlobalMatchBringsEachLightToTheReference)
{
    // The second light is the first doubled and raised by 0.05, sample for sample.
    const QString first = writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u);
    const QString second = writeFlatLight(QStringLiteral("b"), 0.25, 0.0, 0.02, 7u);
    useUnregisteredLights({first, second});

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->mode, NormalizationCoefficientMode::Global);
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::Frames);
    EXPECT_EQ(set->width, 128u);
    EXPECT_EQ(set->height, 96u);
    ASSERT_EQ(set->global.size(), 2u);
    EXPECT_EQ(set->referenceIndex, 0u);
    EXPECT_DOUBLE_EQ(set->global[0].channels[0].scale, 1.0);
    EXPECT_DOUBLE_EQ(set->global[0].channels[0].offset, 0.0);
    EXPECT_NEAR(set->global[1].channels[0].scale, 0.5, 2e-3);
    EXPECT_NEAR(0.25 * set->global[1].channels[0].scale + set->global[1].channels[0].offset, 0.1, 2e-3);

    const QJsonObject sidecar = readJson(sidecarPath());
    EXPECT_EQ(sidecar.value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
    EXPECT_EQ(sidecar.value(QStringLiteral("stageId")).toString(), QStringLiteral("LightsNormalization"));
}

// A light calibrated in memory is not a number where its flat is zero, and the lights as they are read are measured
// without that sample, as the stack reads them. The group failed, because the measurement read the lights as all
// valid and the engine refused the sample.
TEST_F(LightsNormalizationTest, ALightCalibratedInMemoryIsMeasuredWithoutTheSampleItsFlatLeftNotANumber)
{
    // The second light is the first doubled and raised by 0.05, sample for sample.
    const QString first = writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u);
    const QString second = writeFlatLight(QStringLiteral("b"), 0.25, 0.0, 0.02, 7u);
    calibrateInMemoryByAFlat(first, 128u, 96u, std::pair{2u, 3u});
    calibrateInMemoryByAFlat(second, 128u, 96u);
    useUnregisteredLights({first, second});

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_TRUE(result.errorReport.errors.empty()) << result.errorReport.errors.front().toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::Frames);
    ASSERT_EQ(set->global.size(), 2u);
    EXPECT_NEAR(set->global[1].channels[0].scale, 0.5, 2e-3);
    EXPECT_NEAR(0.25 * set->global[1].channels[0].scale + set->global[1].channels[0].offset, 0.1, 2e-3);
}

// On the registration's plane a light calibrated in memory is not a number where its flat is zero, and it is measured
// with that sample read as the plane's no-data zero, as the stack reads it. The group failed, because the measurement
// admitted every sample but an exact zero and the engine refused the sample.
TEST_F(LightsNormalizationTest, ALightCalibratedInMemoryIsMeasuredOnTheRegistrationsPlaneWithoutItsNotANumber)
{
    registerTranslatedPair(4.0, -3.0);
    calibrateInMemoryByAFlat(m_referencePath, an::tests::integration::kStarFieldWidth,
                             an::tests::integration::kStarFieldHeight, std::pair{20u, 20u});
    calibrateInMemoryByAFlat(m_lightPath, an::tests::integration::kStarFieldWidth,
                             an::tests::integration::kStarFieldHeight);

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_TRUE(result.errorReport.errors.empty()) << result.errorReport.errors.front().toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::RegisteredCanvas);
    ASSERT_EQ(set->global.size(), 2u);
    EXPECT_TRUE(std::isfinite(set->global[1].channels[0].scale));
    EXPECT_GT(set->global[1].channels[0].scale, 0.0);
}

TEST_F(LightsNormalizationTest, ASecondRunReusesTheCoefficientsAndASettingsChangeMeasuresAgain)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 0.01, 8u)});
    ASSERT_TRUE(normalize().isSuccess());
    const QByteArray first = contentsOf(sidecarPath());
    const QDateTime written = QFileInfo(artifactPath()).lastModified();

    ASSERT_TRUE(normalize().isSuccess());
    EXPECT_EQ(contentsOf(sidecarPath()), first);
    EXPECT_EQ(QFileInfo(artifactPath()).lastModified(), written);

    updateGroup([](auto& group) { group.stacking.scaleEstimator = ScaleEstimatorMethod::MAD; });
    ASSERT_TRUE(normalize().isSuccess());
    EXPECT_NE(readJson(sidecarPath()).value(QStringLiteral("settingsFingerprint")),
              QJsonDocument::fromJson(first).object().value(QStringLiteral("settingsFingerprint")));
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->global[1].estimator, an::ScaleEstimator::MedianAbsoluteDeviation);
}

TEST_F(LightsNormalizationTest, AnOlderSidecarOrACorruptPayloadIsMeasuredAgain)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 0.01, 8u)});
    ASSERT_TRUE(normalize().isSuccess());

    rewriteJson(sidecarPath(), [](QJsonObject& sidecar) { sidecar.insert(QStringLiteral("schemaVersion"), 5); });
    ASSERT_TRUE(normalize().isSuccess());
    EXPECT_EQ(readJson(sidecarPath()).value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
    ASSERT_TRUE(coefficients().has_value());

    {
        QFile payload(artifactPath());
        ASSERT_TRUE(payload.open(QIODevice::WriteOnly | QIODevice::Truncate));
        payload.write("not a coefficient document");
    }
    ASSERT_TRUE(normalize().isSuccess());
    ASSERT_TRUE(coefficients().has_value());
}

TEST_F(LightsNormalizationTest, ANewerSidecarIsNeitherReadNorReplacedUnlessTheGroupIsReprocessed)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 0.01, 8u)});
    ASSERT_TRUE(normalize().isSuccess());
    rewriteJson(sidecarPath(), [](QJsonObject& sidecar) {
        sidecar.insert(QStringLiteral("schemaVersion"),
                       ImageIntegration::schema::kNormalizationArtifactSchemaVersion + 1);
    });
    const QByteArray sidecar = contentsOf(sidecarPath());
    const QByteArray payload = contentsOf(artifactPath());

    const StageResult refused = normalize();
    EXPECT_TRUE(refused.isFailure() || refused.isPartialSuccess());
    EXPECT_EQ(contentsOf(sidecarPath()), sidecar);
    EXPECT_EQ(contentsOf(artifactPath()), payload);

    m_manifest.reprocessExisting = true;
    ASSERT_TRUE(normalize().isSuccess());
    EXPECT_EQ(readJson(sidecarPath()).value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kNormalizationArtifactSchemaVersion);
}

TEST_F(LightsNormalizationTest, ALocalNormalizationFitsASurfacePerLightAndTheMatchOverIt)
{
    const QString first = writeFlatLight(QStringLiteral("a"), 0.1, 0.0005, 0.005, 7u);
    const QString second = writeFlatLight(QStringLiteral("b"), 0.2, 0.0015, 0.01, 8u);
    useUnregisteredLights({first, second});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.localScaleEnabled = true;
    });

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->mode, NormalizationCoefficientMode::LocalThenGlobal);
    ASSERT_TRUE(set->local.has_value());
    const an::LocalNormalizationDocument& local = *set->local;
    EXPECT_EQ(local.referenceIndex, std::optional<std::uint32_t>(0u));
    EXPECT_EQ(local.validity, an::ValidityRule::FiniteOnly);
    ASSERT_EQ(local.frames.size(), 2u);
    EXPECT_EQ(local.frames[0].background, local.referenceBackground);
    ASSERT_EQ(local.frames[1].background.size(), local.referenceBackground.size());
    EXPECT_NE(local.frames[1].background, local.referenceBackground);
    EXPECT_FALSE(local.frames[1].scaleGrid.empty());
    ASSERT_TRUE(local.frames[1].global.has_value());
    EXPECT_TRUE(std::isfinite(local.frames[1].global->channels[0].scale));
    EXPECT_GT(local.frames[1].background.back(), local.referenceBackground.back());
}

TEST_F(LightsNormalizationTest, TheBestRankedLightIsTheLocalReferenceWhenNoneIsNamed)
{
    // Ranked by their median, the brightest background is the best.
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.10, 0.0005, 0.005, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.30, 0.0005, 0.005, 8u),
                           writeFlatLight(QStringLiteral("c"), 0.20, 0.0005, 0.005, 9u)});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
    });

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_TRUE(set->local.has_value());
    EXPECT_EQ(set->referenceIndex, 1u);
    EXPECT_EQ(set->local->referenceIndex, std::optional<std::uint32_t>(1u));
    EXPECT_EQ(set->local->frames[1].background, set->local->referenceBackground);
    ASSERT_TRUE(set->local->frames[1].global.has_value());
    EXPECT_DOUBLE_EQ(set->local->frames[1].global->channels[0].scale, 1.0);
}

TEST_F(LightsNormalizationTest, TheBestRankedLightsIntegratedAreAReferenceEveryLightIsBroughtTo)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.10, 0.0005, 0.005, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.30, 0.0005, 0.005, 8u),
                           writeFlatLight(QStringLiteral("c"), 0.20, 0.0005, 0.005, 9u)});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Integrate Best Frames");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
        group.stacking.localNormalization.maxIntegratedFrames = 2;
    });

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_TRUE(set->local.has_value());
    const an::LocalNormalizationDocument& local = *set->local;
    // An integrated reference is no frame: the document names none, and the set keeps the light scales were read
    // against.
    EXPECT_FALSE(local.referenceIndex.has_value());
    EXPECT_EQ(set->referenceIndex, 1u);
    ASSERT_EQ(local.frames.size(), 3u);
    EXPECT_NE(local.frames[1].background, local.referenceBackground);
    // The integrated surface lies between the two best lights' (0.30 and 0.20) and above the third's.
    EXPECT_LT(local.referenceBackground.front(), local.frames[1].background.front());
    EXPECT_GT(local.referenceBackground.front(), local.frames[0].background.front());
    for (const auto& record : local.frames)
    {
        ASSERT_TRUE(record.global.has_value());
    }
}

TEST_F(LightsNormalizationTest, NamedReferencesAreIntegratedWhenSeveralAreNamed)
{
    const QString first = writeFlatLight(QStringLiteral("a"), 0.10, 0.0005, 0.005, 7u);
    const QString second = writeFlatLight(QStringLiteral("b"), 0.30, 0.0005, 0.005, 8u);
    const QString third = writeFlatLight(QStringLiteral("c"), 0.20, 0.0005, 0.005, 9u);
    useUnregisteredLights({first, second, third});
    updateGroup([&](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Explicit Reference");
        group.normalizationReferenceFrames = {FrameId(first), FrameId(third)};
    });

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_TRUE(set->local.has_value());
    EXPECT_FALSE(set->local->referenceIndex.has_value());
    EXPECT_EQ(set->referenceIndex, 0u);
}

// A reference chosen by hand that the group does not hold fails the group, naming that light, rather than normalizing
// the group to a light nobody chose.
TEST_F(LightsNormalizationTest, AReferenceChosenByHandThatTheGroupDoesNotHoldFailsTheGroupNamingIt)
{
    const QString leftOut = writeFlatLight(QStringLiteral("a"), 0.10, 0.0, 0.005, 7u);
    useUnregisteredLights({writeFlatLight(QStringLiteral("b"), 0.20, 0.0, 0.005, 8u),
                           writeFlatLight(QStringLiteral("c"), 0.30, 0.0, 0.005, 9u)});
    updateGroup([&](auto& group) {
        group.stacking.normalizationReferenceMode = NormalizationReferenceMode::Manual;
        group.stacking.manualNormalizationReference = FrameId(leftOut).canonicalPath();
        group.normalizationReferenceFrames = {FrameId(leftOut)};
    });

    const StageResult result = normalize();
    const IntegrationError* error = result.isFailure()                   ? &result.error
                                    : !result.errorReport.errors.empty() ? &result.errorReport.errors.front()
                                                                         : nullptr;
    ASSERT_NE(error, nullptr) << "the group is measured";
    EXPECT_EQ(error->code(), IntegrationErrorCode::InvalidConfiguration);
    EXPECT_TRUE(error->toLogString().contains(FrameId(leftOut).canonicalPath())) << error->toLogString().toStdString();
    EXPECT_FALSE(QFileInfo::exists(artifactPath()));
}

TEST_F(LightsNormalizationTest, TheAnalysisFiltersRunBeforeTheSurfacesAreFitted)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0005, 0.005, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0015, 0.01, 8u)});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.hotPixelMedianRadius = 1;
        group.stacking.localNormalization.noiseReductionRadius = 1;
    });
    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_TRUE(set->local.has_value());
    EXPECT_GT(set->local->frames[1].background.back(), set->local->referenceBackground.back());
}

TEST_F(LightsNormalizationTest, AStarFieldsScaleIsReadFromItsStarsFluxes)
{
    registerTranslatedPair(4.0, -3.0);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Star Count");
        group.stacking.localNormalization.scaleEvaluation = QStringLiteral("PSF Flux");
        group.stacking.localNormalization.minDetectionSnr = 0.0;
    });
    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_TRUE(set->local.has_value());
    // The two lights hold the same stars, so their fluxes agree.
    const std::size_t other = set->referenceIndex == 0u ? 1u : 0u;
    EXPECT_NEAR(set->local->frames[other].scale[0], 1.0, 0.1);
}

TEST_F(LightsNormalizationTest, AStopRequestedBeforeTheRunCancelsIt)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 0.01, 7u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 0.01, 8u)});
    CancellationToken token;
    token.requestCancel();
    LightsNormalizationStage stage;
    EXPECT_TRUE(m_run.run(stage, m_manifest, token).isCancelled());
    EXPECT_FALSE(QFileInfo::exists(artifactPath()));
}

TEST_F(LightsNormalizationTest, CurrentRegisteredLightsAreMeasuredOnTheRegistrationsPlane)
{
    registerTranslatedPair(4.0, -3.0);
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::RegisteredCanvas);
    ASSERT_EQ(set->sourceFrames.size(), 2u);
    for (std::size_t index = 0; index < 2u; ++index)
    {
        EXPECT_NE(QFileInfo(set->sourceFrames[index]), QFileInfo(set->logicalFrames[index]));
        EXPECT_TRUE(set->sourceFrames[index].contains(QStringLiteral("registered_lights")));
    }
    EXPECT_EQ(readJson(sidecarPath()).value(QStringLiteral("coefficientProvenance")).toString(),
              QStringLiteral("standard_global"));
}

// A light the registration refused has no registered output and the stack leaves it out, so the normalization measures
// the others as the alignment wrote them - on the plane the stack reads - rather than warping the inputs onto a plane
// planned over this group's lights alone, which an intersection over a registration of several filters makes larger.
TEST_F(LightsNormalizationTest, ALightTheRegistrationRefusedLeavesTheOthersMeasuredAsTheAlignmentWroteThem)
{
    m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
    m_registration.settings.autoReferenceDuringRegistration = false;
    m_registration.settings.enrichWithGaiaCatalog = false;
    m_registration.settings.transformMode = QStringLiteral("Linear");
    m_registration.settings.transformation = QStringLiteral("Affine");
    m_integration.registrationGroupKey = m_registration.groupKey;
    const std::vector<FixtureStar> reference = starCatalogue(30u, 17u);
    m_referencePath = writeStarLight(QStringLiteral("light_001"), reference, 71u);
    m_lightPath =
        writeStarLight(QStringLiteral("light_002"),
                       movedStars(reference, [](double x, double y) { return std::pair{x + 4.0, y - 3.0}; }), 72u);
    // No star to match: the registration refuses it.
    const QString starless = writeStarLight(QStringLiteral("light_003"), {}, 73u);
    m_registration.lightFiles = {FrameId(m_referencePath), FrameId(m_lightPath), FrameId(starless)};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
    m_integration.lightFiles = m_registration.lightFiles;
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};
    LightsRegistrationStage registration;
    const StageResult registered = m_run.run(registration, m_manifest);
    ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::RegisteredCanvas);
    ASSERT_EQ(set->sourceFrames.size(), 2u) << "the refused light is left out";
    for (std::size_t index = 0; index < set->sourceFrames.size(); ++index)
    {
        EXPECT_TRUE(set->sourceFrames[index].contains(QStringLiteral("registered_lights")))
            << set->sourceFrames[index].toStdString() << " is not what the alignment wrote";
        EXPECT_NE(QFileInfo(set->logicalFrames[index]), QFileInfo(starless));
    }
    EXPECT_TRUE(readJson(sidecarPath()).value(QStringLiteral("planeGeometryFingerprint")).toString().isEmpty())
        << "no plane was planned in memory";
}

TEST_F(LightsNormalizationTest, WithoutCurrentRegisteredLightsTheInputsAreWarpedOntoThePlaneInMemory)
{
    registerTranslatedPair(4.0, -3.0);

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::RegisteredCanvas);
    ASSERT_EQ(set->sourceFrames.size(), 2u);
    EXPECT_EQ(QFileInfo(set->sourceFrames[1]), QFileInfo(m_lightPath));
    EXPECT_FALSE(readJson(sidecarPath()).value(QStringLiteral("planeGeometryFingerprint")).toString().isEmpty());
    ASSERT_EQ(set->global.size(), 2u);
    EXPECT_GT(set->global[1].channels[0].scale, 0.0);
}

TEST_F(LightsNormalizationTest, AnOverlapNormalizationReconcilesTheRegisteredLights)
{
    registerTranslatedPair(6.0, 2.0);
    updateGroup([](auto& group) {
        group.stacking.overlapNormalization.enabled = true;
        group.stacking.overlapNormalization.minOverlapPixels = 100;
    });

    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    ASSERT_EQ(set->global.size(), 2u);
    EXPECT_DOUBLE_EQ(set->global[0].channels[0].scale, 1.0);
    EXPECT_NEAR(set->global[1].channels[0].scale, 1.0, 0.1);
}

class LightsDrizzleTest : public NormalizationStageTestBase
{
protected:
    void enableDrizzle(const WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy policy =
                           WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::CompactDescriptorPreferred)
    {
        updateGroup([policy](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = 2.0f;
            settings.pixelFraction = 0.9f;
            settings.mapPersistencePolicy = policy;
            group.drizzle = settings;
        });
    }

    [[nodiscard]] QString descriptorDirectory() const
    {
        return ImageIntegration::drizzleDescriptorDirectory(m_manifest, m_manifest.integrationGroups.front());
    }
};

TEST_F(LightsDrizzleTest, EachLightGetsADescriptorThatValidatesAgainstTheEnginesIdentity)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle();
    const StageResult result = drizzle();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    const auto read =
        ImageIntegration::readDrizzleDescriptorIndex(descriptorDirectory(), m_manifest.integrationGroups.front());
    ASSERT_TRUE(read.index.has_value()) << read.reason.toStdString();
    EXPECT_EQ(read.index->byFrame.size(), 2u);
    EXPECT_GT(read.index->outputWidth, an::tests::integration::kStarFieldWidth);
    for (const auto& [frame, record] : read.index->byFrame)
    {
        (void)frame;
        EXPECT_TRUE(record.densePayloadPath.isEmpty());
        EXPECT_TRUE(QFileInfo::exists(record.descriptorPath));
    }
    EXPECT_EQ(read.index->sidecar.value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion);
}

TEST_F(LightsDrizzleTest, ASecondRunReusesTheDescriptors)
{
    registerTranslatedPair(2.0, 1.0);
    enableDrizzle();
    ASSERT_TRUE(drizzle().isSuccess());
    const QString sidecar = ImageIntegration::drizzleGroupSidecarPath(descriptorDirectory());
    const QByteArray written = contentsOf(sidecar);
    ASSERT_FALSE(written.isEmpty());
    ASSERT_TRUE(drizzle().isSuccess());
    EXPECT_EQ(contentsOf(sidecar), written);
}

TEST_F(LightsDrizzleTest, ADenseCheckpointIsWrittenBesideEachDescriptorAndReadsBack)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle(WorkflowManifest::IntegrationGroup::DrizzleMapPersistencePolicy::DenseCheckpoint);
    ASSERT_TRUE(drizzle().isSuccess());
    const auto read =
        ImageIntegration::readDrizzleDescriptorIndex(descriptorDirectory(), m_manifest.integrationGroups.front());
    ASSERT_TRUE(read.index.has_value()) << read.reason.toStdString();
    for (const auto& [frame, record] : read.index->byFrame)
    {
        (void)frame;
        ASSERT_FALSE(record.densePayloadPath.isEmpty());
        QString error;
        const auto samples = ImageIntegration::readDrizzleDensePayload(record.densePayloadPath, record.document, error);
        ASSERT_TRUE(samples.has_value()) << error.toStdString();
        EXPECT_EQ(static_cast<std::size_t>(samples->size()), static_cast<std::size_t>(record.document.sourceWidth) *
                                                                 record.document.sourceHeight * 2u * sizeof(float));
    }
}

TEST_F(LightsDrizzleTest, ANewerSidecarIsNotReplacedAndADenseMapFileIsLeftAlone)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle();
    ASSERT_TRUE(drizzle().isSuccess());
    const QString sidecar = ImageIntegration::drizzleGroupSidecarPath(descriptorDirectory());
    const QString map_file = QDir(descriptorDirectory()).filePath(QStringLiteral("light_001.pixmap"));
    {
        QFile file(map_file);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("ANPIXMAP");
    }
    rewriteJson(sidecar, [](QJsonObject& object) {
        object.insert(QStringLiteral("schemaVersion"), ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion + 1);
    });
    const QByteArray newer = contentsOf(sidecar);

    const StageResult refused = drizzle();
    EXPECT_TRUE(refused.isFailure() || refused.isPartialSuccess());
    EXPECT_EQ(contentsOf(sidecar), newer);
    EXPECT_TRUE(QFileInfo::exists(map_file));

    m_manifest.reprocessExisting = true;
    ASSERT_TRUE(drizzle().isSuccess());
    EXPECT_EQ(readJson(sidecar).value(QStringLiteral("schemaVersion")).toInt(),
              ImageIntegration::schema::kDrizzleGroupSidecarSchemaVersion);
    EXPECT_TRUE(QFileInfo::exists(map_file));
}

TEST_F(LightsDrizzleTest, AHeaderDistortionIsReadIntoTheDescriptorsWhateverTheProcessLocale)
{
    const CommaDecimalLocale locale;
    if (!locale.active())
    {
        GTEST_SKIP() << "the host installs no locale with a decimal comma";
    }
    m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
    m_integration.registrationGroupKey = m_registration.groupKey;
    const QString reference = writeStarLightWithCards(QStringLiteral("light_001"), 81u);
    const QString light = writeStarLightWithCards(QStringLiteral("light_002"), 82u);
    m_registration.lightFiles = {FrameId(reference), FrameId(light)};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_integration.lightFiles = m_registration.lightFiles;
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};

    // A registration that left each light's own distortion to whatever resamples it.
    an::ui::RegistrationMetricsWriter writer(
        QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db")),
        QStringLiteral("DrizzleTestWriter"));
    ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    ASSERT_TRUE(writer.initialize());
    an::ui::RegistrationGroupState state;
    state.groupKey = m_registration.groupKey.toString();
    state.settingsVersion = m_registration.settings.version;
    state.referenceFrameId = FrameId(reference).canonicalPath();
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.lightFilesHash = QStringLiteral("lights");
    QString error;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    for (const QString& path : {reference, light})
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(path);
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.transformJson = stage_utils::serializeTransformJson(an::RegistrationTransform{});
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    }

    enableDrizzle();
    const StageResult result = drizzle();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto read =
        ImageIntegration::readDrizzleDescriptorIndex(descriptorDirectory(), m_manifest.integrationGroups.front());
    ASSERT_TRUE(read.index.has_value()) << read.reason.toStdString();
    ASSERT_EQ(read.index->byFrame.size(), 2u);
    for (const auto& [frame, record] : read.index->byFrame)
    {
        (void)frame;
        ASSERT_TRUE(record.document.resolvedSourceDistortion.has_value()) << record.descriptorPath.toStdString();
    }
}

// A panel registered beside the reference, overlapping none of it, leaves an intersection no area is common to, and
// the stage failed the group. Its lights are drizzled onto the standard canvas instead - the reference's, at the
// drizzle's scale - and the stage says so.
TEST_F(LightsDrizzleTest, APanelSharingNoAreaWithTheReferenceIsDrizzledOntoTheStandardCanvasAndSaid)
{
    m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
    m_registration.settings.alignmentMode = QStringLiteral("intersection");
    m_integration.registrationGroupKey = m_registration.groupKey;
    const QString reference = writeStarLight(QStringLiteral("light_001"), starCatalogue(30u, 17u), 71u);
    const QString panel = writeStarLight(QStringLiteral("light_002"), starCatalogue(30u, 18u), 72u);
    m_registration.lightFiles = {FrameId(reference), FrameId(panel)};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_integration.lightFiles = m_registration.lightFiles;
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};

    // A registration that placed the panel two hundred columns to the right of the reference.
    const an::Result<an::RegistrationTransform> beside = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Similarity,
                                         .sourceToReference = {1.0, 0.0, 200.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}});
    ASSERT_TRUE(beside.hasValue()) << beside.status().message();
    an::ui::RegistrationMetricsWriter writer(
        QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db")),
        QStringLiteral("DrizzleIntersectionWriter"));
    ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    ASSERT_TRUE(writer.initialize());
    an::ui::RegistrationGroupState state;
    state.groupKey = m_registration.groupKey.toString();
    state.settingsVersion = m_registration.settings.version;
    state.referenceFrameId = FrameId(reference).canonicalPath();
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.lightFilesHash = QStringLiteral("lights");
    QString error;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    for (const auto& [path, transform] : {std::pair{reference, an::RegistrationTransform{}}, std::pair{panel, *beside}})
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(path);
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.transformJson = stage_utils::serializeTransformJson(transform);
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    }

    enableDrizzle();
    const StageResult result = drizzle();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "the group was not drizzled";
    const auto read =
        ImageIntegration::readDrizzleDescriptorIndex(descriptorDirectory(), m_manifest.integrationGroups.front());
    ASSERT_TRUE(read.index.has_value()) << read.reason.toStdString();
    EXPECT_EQ(read.index->byFrame.size(), 2u);
    EXPECT_EQ(read.index->outputWidth, 2u * an::tests::integration::kStarFieldWidth);
    EXPECT_EQ(read.index->outputHeight, 2u * an::tests::integration::kStarFieldHeight);
    const QStringList& warnings = m_run.loggedWarnings();
    EXPECT_EQ(std::count_if(warnings.begin(), warnings.end(),
                            [](const QString& warning) {
                                return warning.contains(QStringLiteral(
                                    "so its intersection canvas is replaced by the standard reference-frame canvas"));
                            }),
              1);
}

TEST_F(LightsDrizzleTest, ADrizzledGroupIsNormalizedOnItsCanvasesAnalysisPlane)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle();
    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto set = coefficients();
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->plane, NormalizationMeasurementPlane::DrizzleAnalysisPlane);
    ASSERT_TRUE(set->drizzlePlane.has_value());
    EXPECT_EQ(set->drizzlePlane->width, set->width);
    EXPECT_EQ(set->drizzlePlane->height, set->height);
    EXPECT_DOUBLE_EQ(set->drizzlePlane->scale, 2.0);
}

// A drizzled Local group's stack fits the surfaces on the drizzled lights itself - the engine applies a document
// measured on the canvas's analysis plane only to frames on that plane - so the stage measures nothing for it, as the
// planner plans. It measured a whole local document, warping every light onto the plane, that the stack then dropped.
TEST_F(LightsDrizzleTest, ADrizzledLocalGroupIsNotMeasuredForAStackThatFitsItsOwnSurfaces)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle();
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
    });

    LightsNormalizationStage stage;
    EXPECT_TRUE(stage.shouldSkip(m_manifest));
    const StageResult result = normalize();
    EXPECT_EQ(result.status, StageResult::Status::Skipped) << result.error.toLogString().toStdString();
    EXPECT_FALSE(QFileInfo::exists(artifactPath()));
    EXPECT_FALSE(QFileInfo::exists(sidecarPath()));
}

// Beside a group the stage measures, a drizzled Local group is passed over with the reason, and only the other group's
// coefficients are published.
TEST_F(LightsDrizzleTest, ADrizzledLocalGroupBesideAMeasuredOneIsPassedOverAndSaid)
{
    registerTranslatedPair(4.0, -3.0);
    enableDrizzle();
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
    });
    WorkflowManifest::IntegrationGroup global = m_integration;
    global.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|lights|Lum2")}};
    global.drizzle.reset();
    global.stacking.normalizationMode = NormalizationMode::Global;
    global.stacking.localNormalization.enabled = false;
    m_manifest.integrationGroups = {m_integration, global};

    LightsNormalizationStage stage;
    EXPECT_FALSE(stage.shouldSkip(m_manifest));
    const StageResult result = normalize();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(
        QFileInfo::exists(stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups[0])));
    EXPECT_TRUE(QFileInfo::exists(stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups[1])));
    const QStringList& details = m_run.loggedDetails();
    EXPECT_TRUE(std::any_of(details.begin(), details.end(), [](const QString& detail) {
        return detail.contains(QStringLiteral("drizzled stack"));
    })) << details.join(QStringLiteral("\n")).toStdString();
}

} // namespace
