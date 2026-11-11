// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights stacking stage. A group's lights are read where its registration put them - as they
/// are, warped onto the registration's plane as they are read, as the registered images the alignment wrote, drizzled
/// onto their canvas, or as the comet's registration aligned them - brought to the reference by the normalization
/// stage's coefficients, weighted by what the registration measured, and stacked in one admitted operation. The master
/// is published once, whole and stamped with the identity a later run reuses it by; its maps carry the same identity
/// and are current only while they do; an unknown stored method stops the group by name and a setting the stack cannot
/// honour is said, as is what the engine warns of a group's stack; a drizzled master keeps its lights' level at every
/// scale and carries the registration's solution on its canvas; the quality record keeps the clustered pixels and
/// every rejected element as two figures; the progress the stage reports moves forward through each group from the
/// stack's own reports; lights with no area common to all of them are placed on the standard canvas by every route,
/// which says so; the per-pixel weighting weighs a light down only where it is noisy; the quality maps hold the
/// coverage, variance, gain and weight sum of what was combined; a fallback solve is tried for a master with no
/// solution to carry; and every drizzle kernel is the one the lights are drizzled with.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"
#include "Test_ImageIntegration/workflow/StackWeightRules.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsDrizzleStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsNormalizationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/NormalizationArtifactSupport.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StackCometProducts.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationMetricsStateLoader.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointF>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureStar;
using an::tests::integration::brightestNear;
using an::tests::integration::CalibrationRun;
using an::tests::integration::exposureStartCard;
using an::tests::integration::movedStars;
using an::tests::integration::readWrittenImage;
using an::tests::integration::starCatalogue;
using an::tests::integration::writeStarField;
using an::tests::integration::writtenIdentity;

constexpr std::uint32_t kLightWidth = 64u;
constexpr std::uint32_t kLightHeight = 48u;

/// The mean of a written image's first plane, read by the neutral reader.
[[nodiscard]] std::optional<double> meanOf(const QString& path)
{
    const auto written = readWrittenImage(path);
    if (!written.has_value() || written->samples.empty())
    {
        return std::nullopt;
    }
    double sum = 0.0;
    for (std::uint32_t y = 0; y < written->samples.height(); ++y)
    {
        for (std::uint32_t x = 0; x < written->samples.width(); ++x)
        {
            sum += written->samples.value(x, y, 0u);
        }
    }
    return sum / (static_cast<double>(written->samples.width()) * written->samples.height());
}

/// The median of a written image's first plane over the samples that are numbers, read by the neutral reader: a
/// drizzled master's border no light reached holds none.
[[nodiscard]] std::optional<double> medianOf(const QString& path)
{
    const auto written = readWrittenImage(path);
    if (!written.has_value() || written->samples.empty())
    {
        return std::nullopt;
    }
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(written->samples.width()) * written->samples.height());
    for (std::uint32_t y = 0; y < written->samples.height(); ++y)
    {
        for (std::uint32_t x = 0; x < written->samples.width(); ++x)
        {
            const double value = written->samples.value(x, y, 0u);
            if (std::isfinite(value))
            {
                values.push_back(value);
            }
        }
    }
    if (values.empty())
    {
        return std::nullopt;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2u);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

[[nodiscard]] bool anyContains(const QStringList& lines, const QString& text)
{
    return std::any_of(lines.begin(), lines.end(), [&text](const QString& line) { return line.contains(text); });
}

class LightsStackingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        ASSERT_TRUE(QDir().mkpath(QDir(m_root.path()).filePath(QStringLiteral("lights"))));
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("stacking-run");
        m_manifest.calibrateLightsInMemory = false;
        m_integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|lights|Lum")}};
        m_integration.outputFilename = QStringLiteral("master_Lum.fits");
        m_integration.filter = QStringLiteral("Lum");
        m_integration.stacking.normalizationMode = NormalizationMode::None;
        m_integration.stacking.rejectionMethod = QStringLiteral("None");
    }

    /// Writes a grey Float32 light of `base + amplitude * u` with the stars and cards stated, the noise `u` fixed by
    /// the seed, of the size stated.
    QString writeFlatLight(const QString& name, double base, double amplitude, std::uint64_t seed,
                           const std::vector<FixtureStar>& stars = {}, const std::vector<FixtureCard>& cards = {},
                           const std::uint32_t width = kLightWidth, const std::uint32_t height = kLightHeight)
    {
        an::tests::fixtures::FixtureImage image;
        image.name = name.toStdString();
        image.width = width;
        image.height = height;
        image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
        image.pattern = an::tests::fixtures::FixturePattern::StarField;
        image.seed = seed;
        an::tests::fixtures::FixtureChannelLevels levels;
        levels.base = base;
        levels.amplitude = amplitude;
        levels.alternate = 1.0;
        image.levels = {levels};
        image.stars = stars;
        image.cards = cards;
        std::string error;
        const auto samples = an::tests::fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        EXPECT_TRUE(
            an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// The light a FITS file holds, written beside it as XISF in Zstandard with its bytes shuffled, in subblocks of the
    /// size stated, with its cards.
    QString compressedCopy(const QString& fits, const std::uint64_t subblockBytes)
    {
        const QString path = QFileInfo(fits).absolutePath() + QLatin1Char('/') + QFileInfo(fits).completeBaseName() +
                             QStringLiteral(".xisf");
        an::Result<an::consumer::OperationScope> scope = m_run.session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        if (!scope.hasValue())
        {
            ADD_FAILURE() << scope.status().message();
            return path;
        }
        const std::string source = fits.toUtf8().toStdString();
        const an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(scope->engine(), scope->execution(), {.path = source, .readMetadata = true});
        const an::Result<an::OutputIntent> intent =
            an::OutputIntent::create({.sampleType = an::SampleType::Float32,
                                      .compression = an::OutputCompression::Zstd,
                                      .byteShuffle = true,
                                      .compressionBlockBytes = subblockBytes});
        if (!decoded.hasValue() || !intent.hasValue())
        {
            ADD_FAILURE() << (decoded.hasValue() ? intent.status().message() : decoded.status().message());
            return path;
        }
        const std::string destination = path.toUtf8().toStdString();
        const an::Result<an::OutputReport> written =
            an::writeImage(scope->engine(), scope->execution(), {.destinationPath = destination, .intent = &*intent},
                           decoded->frame, decoded->metadata);
        EXPECT_TRUE(written.hasValue()) << written.status().message();
        return path;
    }

    void useUnregisteredLights(const std::vector<QString>& paths, const double exposureSeconds = 60.0)
    {
        m_integration.lightFiles.clear();
        m_integration.lightExposureSeconds.clear();
        for (const QString& path : paths)
        {
            m_integration.lightFiles.emplace_back(path);
            m_integration.lightExposureSeconds.push_back(exposureSeconds);
        }
        m_manifest.registrationGroups.clear();
        m_manifest.integrationGroups = {m_integration};
    }

    void updateGroup(const std::function<void(WorkflowManifest::IntegrationGroup&)>& edit)
    {
        edit(m_integration);
        m_manifest.integrationGroups = {m_integration};
    }

    /// Two lights of one star field, the second moved and its background at `lightBackground`, registered - the
    /// reference stating `referenceCards` beside its exposure, the group aligned by `alignmentMode` when one is given,
    /// and both written as an RGGB filter array's samples when `mosaic` is set.
    void registerTranslatedPair(double dx, double dy, double lightBackground = 0.05,
                                const std::vector<FixtureCard>& referenceCards = {},
                                const QString& alignmentMode = QString(), const bool mosaic = false)
    {
        registerMovedPair([dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; }, lightBackground,
                          referenceCards, alignmentMode, mosaic);
    }

    /// The pair as `registerTranslatedPair` registers it, the second light's stars moved by `motion`.
    void registerMovedPair(const std::function<std::pair<double, double>(double, double)>& motion,
                           double lightBackground = 0.05, const std::vector<FixtureCard>& referenceCards = {},
                           const QString& alignmentMode = QString(), const bool mosaic = false)
    {
        m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_registration.settings.autoReferenceDuringRegistration = false;
        m_registration.settings.enrichWithGaiaCatalog = false;
        m_registration.settings.transformMode = QStringLiteral("Linear");
        m_registration.settings.transformation = QStringLiteral("Affine");
        if (!alignmentMode.isEmpty())
        {
            m_registration.settings.alignmentMode = alignmentMode;
        }
        m_integration.registrationGroupKey = m_registration.groupKey;

        m_stars = starCatalogue(30u, 17u);
        m_referencePath = writeStarLight(QStringLiteral("light_001"), m_stars, 71u, 0.05, referenceCards, mosaic);
        m_lightPath =
            writeStarLight(QStringLiteral("light_002"), movedStars(m_stars, motion), 72u, lightBackground, {}, mosaic);
        m_registration.lightFiles = {FrameId(m_referencePath), FrameId(m_lightPath)};
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
        m_integration.lightFiles = m_registration.lightFiles;
        m_integration.lightExposureSeconds = {60.0, 60.0};
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};
        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    }

    /// Three starless lights on one sky, registered from their own solutions with the Intersection canvas: the
    /// reference, and a panel either side of it that overlaps it by sixty columns and the other panel by none, so no
    /// area is common to all three while each overlaps the reference's own plane.
    void registerSidePanels()
    {
        m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_registration.settings.autoReferenceDuringRegistration = false;
        m_registration.settings.enrichWithGaiaCatalog = false;
        m_registration.settings.alignmentMode = QStringLiteral("intersection");
        m_integration.registrationGroupKey = m_registration.groupKey;
        const auto solvedLight = [this](const QString& name, double crpix1, std::uint64_t seed) {
            const QString path =
                QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
            std::vector<FixtureCard> cards = an::tests::integration::tanSolutionCards(crpix1, 60.5);
            cards.push_back(exposureStartCard("2026-03-01T01:00:00"));
            std::string error;
            EXPECT_TRUE(writeStarField(path, {}, cards, seed, error)) << error;
            return path;
        };
        m_referencePath = solvedLight(QStringLiteral("light_001"), 80.5, 71u);
        const QString left = solvedLight(QStringLiteral("light_002"), 180.5, 72u);
        const QString right = solvedLight(QStringLiteral("light_003"), -19.5, 73u);
        m_registration.lightFiles = {FrameId(m_referencePath), FrameId(left), FrameId(right)};
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
        m_integration.lightFiles = m_registration.lightFiles;
        m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};
        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
        ASSERT_FALSE(registered.isPartialSuccess()) << "a panel was left unregistered";
    }

    /// Writes a grey sixteen-bit light of one code, as a capture program writes it.
    QString writeCodeLight(const QString& name, double code)
    {
        an::tests::fixtures::AcquisitionFacts facts;
        facts.kind = "Light Frame";
        an::tests::fixtures::FixtureImage light =
            an::tests::fixtures::acquisitionFrame(name.toStdString(), facts, kLightWidth, kLightHeight);
        light.levels = {an::tests::fixtures::FixtureChannelLevels{.base = code}};
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        std::string error;
        EXPECT_TRUE(
            an::tests::fixtures::writeAcquisitionFrame(light, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// Writes a flat master of one sixteen-bit code, zero at the samples stated.
    QString writeFlatMaster(const QString& name, std::uint32_t width, std::uint32_t height,
                            const std::vector<an::tests::fixtures::FixtureSampleOverride>& zeros = {})
    {
        an::tests::fixtures::AcquisitionFacts facts;
        facts.kind = "Flat Field";
        an::tests::fixtures::FixtureImage flat =
            an::tests::fixtures::acquisitionFrame(name.toStdString(), facts, width, height);
        flat.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 20000.0}};
        flat.overrides = zeros;
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("masters/") + name + QStringLiteral(".fits"));
        std::string error;
        EXPECT_TRUE(an::tests::fixtures::writeAcquisitionFrame(flat, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// Calibrates a light in memory by a flat master, as a run that keeps no calibrated light on disk does.
    void calibrateInMemoryBy(const QString& light, const QString& flat)
    {
        m_manifest.calibrateLightsInMemory = true;
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = FrameId(light);
        params.flatPath = FrameId(flat);
        m_manifest.lightsCalibration.push_back(params);
    }

    /// Calibrates the registered pair in memory, the reference by a flat that is zero at one sample outside the central
    /// third its level is read from - so the reference is not a number there - and the other light by a flat without.
    void calibrateReferenceByADeadFlat()
    {
        const QString dead =
            writeFlatMaster(QStringLiteral("flat_dead"), an::tests::integration::kStarFieldWidth,
                            an::tests::integration::kStarFieldHeight,
                            {an::tests::fixtures::FixtureSampleOverride{.x = kDeadX, .y = kDeadY, .value = 0.0}});
        const QString flat = writeFlatMaster(QStringLiteral("flat"), an::tests::integration::kStarFieldWidth,
                                             an::tests::integration::kStarFieldHeight);
        calibrateInMemoryBy(m_referencePath, dead);
        calibrateInMemoryBy(m_lightPath, flat);
    }

    /// Where the dead flat of a registered pair is zero.
    static constexpr std::uint32_t kDeadX = 20u;
    static constexpr std::uint32_t kDeadY = 20u;

    QString writeStarLight(const QString& name, const std::vector<FixtureStar>& stars, std::uint64_t seed,
                           double background = 0.05, std::vector<FixtureCard> cards = {}, const bool mosaic = false)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        cards.push_back(exposureStartCard("2026-03-01T01:00:00"));
        std::string error;
        if (mosaic)
        {
            EXPECT_TRUE(an::tests::integration::writeMosaicStarField(path, stars, cards, seed, error)) << error;
            return path;
        }
        EXPECT_TRUE(writeStarField(path, stars, cards, seed, error, background)) << error;
        return path;
    }

    StageResult stack()
    {
        LightsStackingStage stage;
        return m_run.run(stage, m_manifest);
    }

    [[nodiscard]] QString masterPath() const
    {
        return QDir(m_manifest.outputDirectory).filePath(m_integration.outputFilename);
    }

    [[nodiscard]] QString metricsPath() const
    {
        return QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    }

    /// Three grey lights, a to c, registered where they are, each light's record written by `measure`; the group is
    /// weighted as `weighting` names and its registered images are discardable, so the stack reads the lights.
    std::vector<QString>
    useMeasuredLights(const QString& weighting,
                      const std::function<void(std::size_t, an::ui::RegistrationMetricRecord&)>& measure)
    {
        const std::vector<QString> lights{writeFlatLight(QStringLiteral("a"), 0.2, 0.01, 91u),
                                          writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 92u),
                                          writeFlatLight(QStringLiteral("c"), 0.2, 0.01, 93u)};
        m_registration.groupKey = GroupKey{QStringLiteral("reg|64x48|Lum")};
        m_registration.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_integration.registrationGroupKey = m_registration.groupKey;
        m_integration.lightFiles = m_registration.lightFiles;
        m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
        m_integration.stacking.weighting = weighting;
        m_integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};

        EXPECT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
        an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("StackingTestMeasuredWriter"));
        EXPECT_TRUE(writer.initialize());
        an::ui::RegistrationGroupState state;
        state.groupKey = m_registration.groupKey.toString();
        state.settingsVersion = m_registration.settings.version;
        state.referenceFrameId = FrameId(lights[0]).canonicalPath();
        state.detectionHash = QStringLiteral("detection");
        state.registrationHash = QStringLiteral("registration");
        state.lightFilesHash = QStringLiteral("lights");
        QString error;
        EXPECT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
        for (std::size_t index = 0; index < lights.size(); ++index)
        {
            an::ui::RegistrationMetricRecord metric;
            metric.frameId = FrameId(lights[index]);
            metric.groupKey = state.groupKey;
            metric.settingsVersion = state.settingsVersion;
            metric.metrics.succeeded = true;
            metric.transformJson = stage_utils::serializeTransformJson(an::RegistrationTransform{});
            measure(index, metric);
            EXPECT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
        }
        return lights;
    }

    /// Drizzles the group at the scale stated with a whole drop and the Square kernel.
    void drizzleWholeDrops(const float scale)
    {
        updateGroup([scale](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = scale;
            settings.pixelFraction = 1.0f;
            settings.kernel = QStringLiteral("Square");
            group.drizzle = settings;
        });
    }

    /// Measures the group's normalization, drizzles its lights and stacks them, each stage succeeding.
    StageResult normalizeDrizzleAndStack()
    {
        LightsNormalizationStage normalization;
        const StageResult normalized = m_run.run(normalization, m_manifest);
        EXPECT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
        LightsDrizzleStage drizzle;
        const StageResult drizzled = m_run.run(drizzle, m_manifest);
        EXPECT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
        return stack();
    }

    /// The pair `registerTranslatedPair` registers, the second light brighter, with every transform restated as the
    /// whole-pixel shift the stars were moved by: the registration measures the shift from the stars to a fraction of a
    /// pixel only, and a whole shift makes a light's registered image and its drizzle with a whole drop at a scale of
    /// one the same samples.
    ///
    /// @param shift receives the second light's shift onto the reference, in whole pixels.
    void registerWholePixelPair(std::pair<double, double>& shift)
    {
        registerTranslatedPair(4.0, -3.0, 0.10);
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("WholePixelReader"));
        ASSERT_TRUE(loader.open());
        std::vector<an::ui::RegistrationMetricRecord> records;
        ASSERT_TRUE(
            loader.loadMetricRecords(m_registration.groupKey.toString(), m_registration.settings.version, records));
        ASSERT_EQ(records.size(), 2u);
        an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("WholePixelWriter"));
        ASSERT_TRUE(writer.openExisting());
        for (an::ui::RegistrationMetricRecord& record : records)
        {
            QString error;
            const std::optional<stage_utils::ParsedTransformRecord> parsed =
                stage_utils::parseTransformRecord(record.transformJson, error);
            ASSERT_TRUE(parsed.has_value()) << error.toStdString();
            const std::array<double, 9>& solved = parsed->transform.matrix();
            const double dx = std::round(solved[2]);
            const double dy = std::round(solved[5]);
            const an::Result<an::RegistrationTransform> whole =
                an::RegistrationTransform::create({.linearKind = an::RegistrationTransformKind::Similarity,
                                                   .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
            ASSERT_TRUE(whole.hasValue()) << whole.status().message();
            record.transformJson = stage_utils::serializeTransformJson(*whole, parsed->provenance);
            ASSERT_TRUE(writer.upsertMetric(record, &error)) << error.toStdString();
            if (record.frameId == FrameId(m_lightPath))
            {
                shift = {dx, dy};
            }
        }
    }

    /// The pair normalized locally to its first light and stacked as registered images; the master that writes.
    std::optional<an::tests::fixtures::FixtureSamples> stackRegisteredLocalPair()
    {
        updateGroup([](auto& group) {
            group.stacking.normalizationMode = NormalizationMode::Local;
            group.stacking.localNormalization.enabled = true;
            group.normalizationReferenceFrames = {group.lightFiles.front()};
        });
        LightsAlignmentStage alignment;
        const StageResult aligned = m_run.run(alignment, m_manifest);
        EXPECT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
        LightsNormalizationStage normalization;
        const StageResult normalized = m_run.run(normalization, m_manifest);
        EXPECT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
        const StageResult stacked = stack();
        EXPECT_TRUE(stacked.isSuccess()) << stacked.error.toLogString().toStdString();
        const auto written = readWrittenImage(masterPath());
        if (!written.has_value())
        {
            return std::nullopt;
        }
        return written->samples;
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
    WorkflowManifest::RegistrationGroup m_registration;
    WorkflowManifest::IntegrationGroup m_integration;
    std::vector<FixtureStar> m_stars;
    QString m_referencePath;
    QString m_lightPath;
};

TEST_F(LightsStackingTest, TheMasterIsTheAverageOfItsLightsPublishedWholeAndStamped)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.3, 0.0, 3u)});
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    EXPECT_TRUE(result.groupResults.front().success);
    EXPECT_TRUE(result.groupResults.front().committedArtifact.has_value());
    EXPECT_EQ(result.groupResults.front().lightFrameCount, 3);
    // Float32 lights read as they are, none calibrated or warped, are read a band at a time.
    EXPECT_TRUE(anyContains(m_run.loggedDetails(), QStringLiteral("Frame reads: Bands of rows")))
        << m_run.loggedDetails().join(QLatin1Char('\n')).toStdString();

    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->samples.width(), kLightWidth);
    EXPECT_EQ(written->samples.height(), kLightHeight);
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 0.2, 1e-6);
    ASSERT_TRUE(written->card("TOTEXP").has_value());
    EXPECT_DOUBLE_EQ(std::stod(*written->card("TOTEXP")), 180.0);

    const auto stamp = writtenIdentity(m_run.session(), masterPath());
    ASSERT_TRUE(stamp.has_value());
    EXPECT_FALSE(stamp->artifactId.empty());
    EXPECT_EQ(stamp->runId, "stacking-run");
    const QStringList pending =
        QDir(m_manifest.outputDirectory).entryList({QStringLiteral("*.anpending*")}, QDir::Files);
    EXPECT_TRUE(pending.isEmpty()) << pending.join(QStringLiteral(", ")).toStdString();
}

TEST_F(LightsStackingTest, TheMasterCarriesTheCardsItsReferenceLightsHeaderStates)
{
    const FixtureCard object{"OBJECT", "M42", "", an::tests::fixtures::FixtureCardKind::Text};
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u, {}, {object}),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u, {}, {object})});
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    // The stage reads the reference light's header by its path when it writes the master's cards.
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    ASSERT_TRUE(written->card("OBJECT").has_value());
    EXPECT_NE(written->card("OBJECT")->find("M42"), std::string::npos) << *written->card("OBJECT");
}

TEST_F(LightsStackingTest, ASecondRunReusesTheMasterAndAChangedLightIsStackedAgain)
{
    const QString changing = writeFlatLight(QStringLiteral("c"), 0.3, 0.0, 3u);
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u), changing});
    updateGroup([](auto& group) { group.skipIfExists = true; });
    ASSERT_TRUE(stack().isSuccess());
    const QDateTime written = QFileInfo(masterPath()).lastModified();
    const auto firstStamp = writtenIdentity(m_run.session(), masterPath());
    ASSERT_TRUE(firstStamp.has_value());

    ASSERT_TRUE(stack().isSuccess());
    EXPECT_EQ(QFileInfo(masterPath()).lastModified(), written);

    writeFlatLight(QStringLiteral("c"), 0.6, 0.0, 3u);
    {
        QFile file(changing);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
    }
    ASSERT_TRUE(stack().isSuccess());
    const auto restacked = readWrittenImage(masterPath());
    ASSERT_TRUE(restacked.has_value());
    EXPECT_NEAR(restacked->samples.value(10u, 10u, 0u), 0.3, 1e-6);
    const auto secondStamp = writtenIdentity(m_run.session(), masterPath());
    ASSERT_TRUE(secondStamp.has_value());
    EXPECT_NE(secondStamp->artifactId, firstStamp->artifactId);
}

TEST_F(LightsStackingTest, AnUnknownStoredMethodStopsTheGroupByName)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    updateGroup([](auto& group) { group.stacking.stackMethod = QStringLiteral("Fancy Mean"); });
    const StageResult result = stack();
    EXPECT_TRUE(result.isFailure());
    EXPECT_TRUE(result.error.toLogString().contains(QStringLiteral("Fancy Mean")))
        << result.error.toLogString().toStdString();
    EXPECT_FALSE(QFileInfo::exists(masterPath()));
}

// Lights stored compressed are combined in tiles of the rows one compressed piece of theirs spans, where a sample
// history's cache band would be narrower and decode each piece again for every band: three lights 4096 pixels wide, in
// subblocks of 256 KiB that hold 64 rows of each byte's stretch, are clipped in two tiles of 64 rows, and the master is
// still their average.
TEST_F(LightsStackingTest, CompressedLightsAreCombinedInTilesOfTheRowsTheirPiecesSpan)
{
    constexpr std::uint32_t kWide = 4096u;
    constexpr std::uint32_t kTall = 128u;
    constexpr std::uint64_t kSubblock = std::uint64_t{256u} << 10u;
    useUnregisteredLights(
        {compressedCopy(writeFlatLight(QStringLiteral("wide_a"), 0.1, 0.0, 1u, {}, {}, kWide, kTall), kSubblock),
         compressedCopy(writeFlatLight(QStringLiteral("wide_b"), 0.2, 0.0, 2u, {}, {}, kWide, kTall), kSubblock),
         compressedCopy(writeFlatLight(QStringLiteral("wide_c"), 0.3, 0.0, 3u, {}, {}, kWide, kTall), kSubblock)});
    updateGroup([](auto& group) { group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping"); });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const QString details = m_run.loggedDetails().join(QLatin1Char('\n'));
    EXPECT_TRUE(details.contains(QStringLiteral("Frame reads: Bands of rows"))) << details.toStdString();
    EXPECT_TRUE(details.contains(QStringLiteral("Stack tiles: 2 tiles of 64 rows"))) << details.toStdString();

    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->samples.width(), kWide);
    EXPECT_EQ(written->samples.height(), kTall);
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 0.2, 1e-6);
    EXPECT_NEAR(written->samples.value(kWide - 1u, kTall - 1u, 0u), 0.2, 1e-6);
}

TEST_F(LightsStackingTest, ARejectionTwoLightsCannotRunIsLeftOutAndSaid)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.3, 0.0, 2u)});
    updateGroup([](auto& group) { group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping"); });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("needs at least")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(3u, 3u, 0u), 0.2, 1e-6);
}

TEST_F(LightsStackingTest, AWeightingAMedianCannotTakeIsSaid)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.3, 0.0, 3u)});
    updateGroup([](auto& group) {
        group.stacking.stackMethod = QStringLiteral("Median");
        group.stacking.weighting = QStringLiteral("Noise");
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("takes no frame weights")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
}

TEST_F(LightsStackingTest, TheMapsBelongToTheirMasterByItsIdentityNotByTheirTime)
{
    std::vector<QString> lights;
    for (std::uint64_t index = 0; index < 5u; ++index)
    {
        lights.push_back(writeFlatLight(QStringLiteral("l%1").arg(index), 0.2, 0.02, 11u + index));
    }
    useUnregisteredLights(lights);
    updateGroup([](auto& group) {
        group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
        group.stacking.sigmaLow = 1.5;
        group.stacking.sigmaHigh = 1.5;
        group.stacking.createRejectionMaps = true;
        group.stacking.mergeRejectionMaps = true;
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.groupResults.front().rejectionMaps.size(), 1u);
    const QString map = result.groupResults.front().rejectionMaps.front().path;
    ASSERT_TRUE(QFileInfo::exists(map));

    const auto masterStamp = writtenIdentity(m_run.session(), masterPath());
    const auto mapStamp = writtenIdentity(m_run.session(), map);
    ASSERT_TRUE(masterStamp.has_value());
    ASSERT_TRUE(mapStamp.has_value());
    EXPECT_EQ(mapStamp->artifactId, masterStamp->artifactId);

    const ArtifactTracker tracker(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
    EXPECT_TRUE(ImageIntegration::isStackingAuxiliaryOutputCurrent(tracker, masterPath(), map));

    // A file put in the map's place after the master - newer, and nobody's map - is not the master's.
    ASSERT_TRUE(QFile::remove(map));
    ASSERT_TRUE(QFile::copy(lights.front(), map));
    {
        QFile file(map);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
    }
    const ArtifactTracker reread(m_manifest.outputDirectory, nullptr, sessionArtifactStampReader(m_run.session()));
    EXPECT_FALSE(ImageIntegration::isStackingAuxiliaryOutputCurrent(reread, masterPath(), map));
}

TEST_F(LightsStackingTest, TheQualityRecordKeepsClusteredPixelsAndEveryRejectedElementApart)
{
    std::vector<QString> lights;
    for (std::uint64_t index = 0; index < 6u; ++index)
    {
        // The fourth light carries a bright blob no other light has, which the rejection takes out as one cluster.
        const std::vector<FixtureStar> blob =
            index == 3u ? std::vector<FixtureStar>{FixtureStar{30.0, 24.0, 3.0, 0.7}} : std::vector<FixtureStar>{};
        lights.push_back(writeFlatLight(QStringLiteral("l%1").arg(index), 0.2, 0.01, 31u + index, blob));
    }
    useUnregisteredLights(lights);
    updateGroup([](auto& group) {
        group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
        group.stacking.sigmaLow = 2.0;
        group.stacking.sigmaHigh = 2.0;
        group.stacking.spatialRejectionClustering = true;
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.valid);
    EXPECT_TRUE(quality.hasRejectionStats);
    EXPECT_GE(quality.spatialClusterCount, 1u);
    EXPECT_GT(quality.spatialClusterPixels, 0u);
    EXPECT_GT(quality.spatialRejectedElements, 0u);
    EXPECT_GE(quality.spatialRejectedElements, quality.spatialClusterPixels);
    EXPECT_NEAR(quality.spatialRejectedElementFraction,
                static_cast<double>(quality.spatialRejectedElements) / (kLightWidth * kLightHeight), 1e-12);
    // No map was asked for, so none is written beside the master.
    EXPECT_TRUE(result.groupResults.front().rejectionMaps.empty());
    EXPECT_FALSE(
        QFileInfo::exists(QDir(m_manifest.outputDirectory).filePath(QStringLiteral("master_Lum_rejection.fits"))));
}

TEST_F(LightsStackingTest, TheProgressMovesForwardThroughTheGroupFromTheStacksReports)
{
    std::vector<QString> lights;
    for (std::uint64_t index = 0; index < 4u; ++index)
    {
        lights.push_back(writeFlatLight(QStringLiteral("l%1").arg(index), 0.2, 0.01, 41u + index));
    }
    useUnregisteredLights(lights);
    ASSERT_TRUE(stack().isSuccess());
    const auto& reports = m_run.progressReports();
    ASSERT_FALSE(reports.empty());
    // The group says it is preparing before it reads anything.
    EXPECT_EQ(reports.front().currentStage, WorkflowStage::LightsStacking);
    EXPECT_EQ(reports.front().currentGroup, 1);
    EXPECT_EQ(reports.front().totalGroups, 1);
    EXPECT_FLOAT_EQ(reports.front().stageProgress, 0.0f);
    EXPECT_EQ(reports.front().currentOperation, QStringLiteral("Preparing stacking"));
    float last = -1.0f;
    bool inStack = false;
    QStringList seen;
    for (const WorkflowProgress& report : reports)
    {
        seen << QStringLiteral("%1 %2").arg(report.stageProgress).arg(report.currentOperation);
        EXPECT_GE(report.stageProgress, last) << report.currentOperation.toStdString();
        last = report.stageProgress;
        inStack = inStack || (report.currentOperation == QStringLiteral("Lights stacking") &&
                              report.stageProgress > 0.10f && report.stageProgress <= 0.90f);
    }
    EXPECT_TRUE(inStack) << seen.join(QLatin1Char('\n')).toStdString();
    EXPECT_FLOAT_EQ(reports.back().stageProgress, 1.0f);
}

TEST_F(LightsStackingTest, AStopRequestedBeforeTheRunCancelsIt)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    CancellationToken token;
    token.requestCancel();
    LightsStackingStage stage;
    EXPECT_TRUE(m_run.run(stage, m_manifest, token).isCancelled());
    EXPECT_FALSE(QFileInfo::exists(masterPath()));
}

TEST_F(LightsStackingTest, TheStageIsRefusedOutsideARun)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    LightsStackingStage stage;
    CancellationToken token;
    ProgressReporter reporter;
    EXPECT_FALSE(stage.execute(m_manifest, token, reporter).isSuccess());
    EXPECT_FALSE(QFileInfo::exists(masterPath()));
}

TEST_F(LightsStackingTest, TheNormalizationStagesCoefficientsBringEachLightToTheReference)
{
    // The second light is the first doubled and raised by 0.05, sample for sample.
    const QString reference = writeFlatLight(QStringLiteral("a"), 0.1, 0.01, 7u);
    useUnregisteredLights({reference, writeFlatLight(QStringLiteral("b"), 0.25, 0.02, 7u)});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Global;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsNormalizationStage normalization;
    ASSERT_TRUE(m_run.run(normalization, m_manifest).isSuccess());

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const auto stacked = meanOf(masterPath());
    const auto expected = meanOf(reference);
    ASSERT_TRUE(stacked.has_value());
    ASSERT_TRUE(expected.has_value());
    EXPECT_NEAR(*stacked, *expected, 2e-3);

    // A document that cannot be read is said, and the stack measures its own.
    {
        QFile payload(stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front()));
        ASSERT_TRUE(payload.open(QIODevice::WriteOnly | QIODevice::Truncate));
        payload.write("not a coefficient document");
    }
    m_manifest.reprocessExisting = true;
    const StageResult remeasured = stack();
    ASSERT_TRUE(remeasured.isSuccess()) << remeasured.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(remeasured.warnings, QStringLiteral("could not be read")))
        << remeasured.warnings.join(QStringLiteral("\n")).toStdString();
    const auto measured = meanOf(masterPath());
    ASSERT_TRUE(measured.has_value());
    EXPECT_NEAR(*measured, *expected, 2e-3);
}

TEST_F(LightsStackingTest, AnExposureMergeTakesTheShorterExposureWhereTheLongerSaturates)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("long1"), 0.98, 0.0, 1u),
                           writeFlatLight(QStringLiteral("long2"), 0.98, 0.0, 2u),
                           writeFlatLight(QStringLiteral("short1"), 0.25, 0.0, 3u),
                           writeFlatLight(QStringLiteral("short2"), 0.25, 0.0, 4u)});
    updateGroup([](auto& group) {
        group.lightExposureSeconds = {60.0, 60.0, 15.0, 15.0};
        group.stacking.hdr.enabled = true;
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    // Where the long exposure is past the highlight the short one, brought to the long one's scale, takes over.
    EXPECT_NEAR(written->samples.value(5u, 5u, 0u), 1.0, 1e-5);
}

TEST_F(LightsStackingTest, AnExposureMergeOfOneExposureIsRefused)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.2, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    updateGroup([](auto& group) { group.stacking.hdr.enabled = true; });
    EXPECT_TRUE(stack().isFailure());
    EXPECT_FALSE(QFileInfo::exists(masterPath()));
}

TEST_F(LightsStackingTest, AnRgbSetIsCombinedIntoAColourMasterStamped)
{
    const GroupKey registration{QStringLiteral("reg|rgb")};
    std::vector<WorkflowManifest::IntegrationGroup> groups;
    const std::vector<std::pair<QString, double>> filters{
        {QStringLiteral("Red"), 0.2}, {QStringLiteral("Green"), 0.3}, {QStringLiteral("Blue"), 0.4}};
    std::uint64_t seed = 51u;
    for (const auto& [filter, level] : filters)
    {
        WorkflowManifest::IntegrationGroup group = m_integration;
        group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|lights|") + filter}};
        group.filter = filter;
        group.outputFilename = QStringLiteral("master_%1.fits").arg(filter);
        group.registrationGroupKey = registration;
        group.lightFiles = {FrameId(writeFlatLight(filter + QStringLiteral("1"), level, 0.01, seed++)),
                            FrameId(writeFlatLight(filter + QStringLiteral("2"), level, 0.01, seed++))};
        group.lightExposureSeconds = {60.0, 60.0};
        groups.push_back(group);
    }
    m_manifest.integrationGroups = groups;
    m_manifest.combineRgbImages = true;
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    const QString colour =
        QDir(m_manifest.outputDirectory).filePath(ImageIntegration::combinedRgbOutputFilename(registration.toString()));
    const auto written = readWrittenImage(colour);
    ASSERT_TRUE(written.has_value()) << colour.toStdString() << "\n"
                                     << result.warnings.join(QLatin1Char('\n')).toStdString() << "\n"
                                     << m_run.loggedWarnings().join(QLatin1Char('\n')).toStdString();
    EXPECT_EQ(written->samples.channelCount(), 3u);
    EXPECT_TRUE(written->card("RGBCOMB").has_value());
    EXPECT_TRUE(writtenIdentity(m_run.session(), colour).has_value());
    EXPECT_EQ(result.groupResults.size(), 4u);
}

TEST_F(LightsStackingTest, TransformOnReadStacksTheRegistrationInputsOnTheReferencesPlane)
{
    registerTranslatedPair(4.0, -3.0);
    updateGroup([](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable; });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Transform-on-read")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    // A light warped as it is read is read whole, and the log says why.
    EXPECT_TRUE(anyContains(m_run.loggedDetails(), QStringLiteral("Whole frames: the lights are warped")))
        << m_run.loggedDetails().join(QLatin1Char('\n')).toStdString();
    // Nothing was written below the registered lights: the inputs were warped as the stack read them.
    EXPECT_FALSE(QDir(QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registered_lights"))).exists());

    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->samples.width(), an::tests::integration::kStarFieldWidth);
    const FixtureStar& star = m_stars.front();
    const auto brightest = brightestNear(masterPath(), star.x, star.y, 3);
    ASSERT_TRUE(brightest.has_value());
    EXPECT_LE(std::abs(static_cast<double>(brightest->x) - star.x), 1.0);
    EXPECT_LE(std::abs(static_cast<double>(brightest->y) - star.y), 1.0);
}

TEST_F(LightsStackingTest, RegisteredImagesAreStackedAndOneChangedSinceIsRefused)
{
    registerTranslatedPair(4.0, -3.0);
    LightsAlignmentStage alignment;
    ASSERT_TRUE(m_run.run(alignment, m_manifest).isSuccess());

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Materialized")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    // The registered images are files the stack combines as they are, so it reads them a band at a time.
    EXPECT_TRUE(anyContains(m_run.loggedDetails(), QStringLiteral("Frame reads: Bands of rows")))
        << m_run.loggedDetails().join(QLatin1Char('\n')).toStdString();

    const QString registered =
        QDir(m_manifest.outputDirectory)
            .filePath(QStringLiteral("registered_lights/%1/light_002_reg.fits")
                          .arg(ImageIntegration::sanitizeFilename(m_registration.groupKey.toString())));
    ASSERT_TRUE(QFileInfo::exists(registered));
    {
        QFile file(registered);
        ASSERT_TRUE(file.open(QIODevice::Append));
        file.write(QByteArray(2880, ' '));
    }
    m_manifest.reprocessExisting = true;
    const StageResult refused = stack();
    EXPECT_TRUE(refused.isFailure());
    EXPECT_TRUE(refused.error.toLogString().contains(QStringLiteral("changed after it was persisted")))
        << refused.error.toLogString().toStdString();
}

// A light the registration places a fractional shift from its reference - 4.3 pixels across and 2.6 up - holds nothing
// where it does not reach, so the stack leaves it out there: wherever the reference alone reaches, in the three rows
// before the light's first and the four columns past its last, the master is the reference's own sample, whether the
// stack warps the lights as it reads them or reads the registered images.
TEST_F(LightsStackingTest, WhereOneLightAloneReachesTheMasterIsThatLightsSample)
{
    registerTranslatedPair(4.3, -2.6);
    const auto reference = readWrittenImage(m_referencePath);
    ASSERT_TRUE(reference.has_value());
    const std::uint32_t width = reference->samples.width();
    const std::uint32_t height = reference->samples.height();
    for (const bool warpedAsRead : {true, false})
    {
        SCOPED_TRACE(warpedAsRead ? "the lights warped as the stack reads them" : "the registered images");
        if (warpedAsRead)
        {
            updateGroup(
                [](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable; });
        }
        else
        {
            updateGroup([](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Retained; });
            m_manifest.reprocessExisting = true;
            LightsAlignmentStage alignment;
            ASSERT_TRUE(m_run.run(alignment, m_manifest).isSuccess());
        }
        const StageResult result = stack();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(
            warpedAsRead ? QStringLiteral("Transform-on-read") : QStringLiteral("Materialized")))
            << result.groupResults.front().registrationInputPolicy.toStdString();
        const auto master = readWrittenImage(masterPath());
        ASSERT_TRUE(master.has_value());
        ASSERT_EQ(master->samples.width(), width);
        ASSERT_EQ(master->samples.height(), height);
        std::uint32_t differing = 0u;
        double worst = 0.0;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                // The reference's pixel (x, y) is the light's (x + 4.3, y - 2.6).
                if (y >= 3u && x + 4u < width)
                {
                    continue;
                }
                const double difference =
                    std::abs(master->samples.value(x, y, 0u) - reference->samples.value(x, y, 0u));
                differing += difference != 0.0 ? 1u : 0u;
                worst = (std::max)(worst, difference);
            }
        }
        EXPECT_EQ(differing, 0u) << "the master differs from the reference by up to " << worst;
    }
}

// A stack bounded by memory reads the registered images a band at a time, as one that is not: the planner tiles it
// rather than re-reading every image whole for every tile, and the master is the one the unbounded stack writes,
// sample for sample.
TEST_F(LightsStackingTest, AStackBoundedByMemoryReadsBandsAndWritesTheMasterTheUnboundedOneWrites)
{
    registerTranslatedPair(4.0, -3.0);
    LightsAlignmentStage alignment;
    ASSERT_TRUE(m_run.run(alignment, m_manifest).isSuccess());
    updateGroup([](auto& group) { group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping"); });
    const StageResult first = stack();
    ASSERT_TRUE(first.isSuccess()) << first.error.toLogString().toStdString();
    const auto unbounded = readWrittenImage(masterPath());
    ASSERT_TRUE(unbounded.has_value());

    updateGroup([](auto& group) {
        group.stacking.limitMemory = true;
        group.stacking.maxMemoryMb = 1;
    });
    m_manifest.reprocessExisting = true;
    const StageResult bounded = stack();
    ASSERT_TRUE(bounded.isSuccess()) << bounded.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(m_run.loggedDetails(), QStringLiteral("Frame reads: Bands of rows")))
        << m_run.loggedDetails().join(QLatin1Char('\n')).toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    const std::span<const std::byte> expected = unbounded->samples.bytes();
    const std::span<const std::byte> actual = written->samples.bytes();
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
}

// A light calibrated in memory is not a number where its flat is zero, and the stack of the lights as they are read
// leaves that sample out: the master there is the other lights' mean, and everywhere else all three lights'. The group
// failed, because the stack read the lights as all valid and the engine refused the sample.
TEST_F(LightsStackingTest, ALightCalibratedInMemoryIsLeftOutWhereItsFlatIsZero)
{
    const QString first = writeCodeLight(QStringLiteral("a"), 10000.0);
    const QString second = writeCodeLight(QStringLiteral("b"), 20000.0);
    const QString third = writeCodeLight(QStringLiteral("c"), 30000.0);
    // The dead sample lies outside the central third the flat's level is read from, so both flats divide by one.
    const QString dead = writeFlatMaster(QStringLiteral("flat_dead"), kLightWidth, kLightHeight,
                                         {an::tests::fixtures::FixtureSampleOverride{.x = 2u, .y = 3u, .value = 0.0}});
    const QString flat = writeFlatMaster(QStringLiteral("flat"), kLightWidth, kLightHeight);
    calibrateInMemoryBy(first, dead);
    calibrateInMemoryBy(second, flat);
    calibrateInMemoryBy(third, flat);
    useUnregisteredLights({first, second, third});

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    // A light calibrated as it is read is read whole, to apply its masters, and the log says why.
    EXPECT_TRUE(anyContains(m_run.loggedDetails(), QStringLiteral("Whole frames: the lights are calibrated")))
        << m_run.loggedDetails().join(QLatin1Char('\n')).toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 20000.0 / 65535.0, 1e-6);
    const double there = written->samples.value(2u, 3u, 0u);
    EXPECT_TRUE(std::isfinite(there));
    EXPECT_NEAR(there, 25000.0 / 65535.0, 1e-6) << "the dead sample's light is left out, the other two averaged";
}

// Calibrating in memory, a cosmetic correction asked for with no dark to find its pixels in corrects nothing, and the
// stack says so once for the lights sharing that calibration, as a run that saves its calibrated lights does. It was
// said only on that route.
TEST_F(LightsStackingTest, ACosmeticCorrectionWithoutADarkInMemoryIsSaidOnce)
{
    const QString flat = writeFlatMaster(QStringLiteral("flat"), kLightWidth, kLightHeight);
    std::vector<QString> lights;
    for (const double code : {10000.0, 20000.0, 30000.0})
    {
        lights.push_back(writeCodeLight(QStringLiteral("light_%1").arg(code, 0, 'f', 0), code));
        calibrateInMemoryBy(lights.back(), flat);
        m_manifest.lightsCalibration.back().settings.cosmeticCorrection = true;
    }
    useUnregisteredLights(lights);

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto said = std::count_if(result.warnings.begin(), result.warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("no dark master"));
    });
    EXPECT_EQ(said, 1) << result.warnings.join(QStringLiteral(" | ")).toStdString();
}

// Calibrating in memory, a light the dark-scale optimization cannot measure - smaller than its central square - is
// calibrated with the fixed factor, and the stack says so once for the lights sharing that calibration.
TEST_F(LightsStackingTest, ADarkScaleTheOptimizationCannotMeasureInMemoryIsSaidOnce)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = "Dark Frame";
    an::tests::fixtures::FixtureImage darkFrame =
        an::tests::fixtures::acquisitionFrame("master_dark", facts, kLightWidth, kLightHeight);
    darkFrame.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 1000.0}};
    const QString dark = QDir(m_root.path()).filePath(QStringLiteral("masters/master_dark.fits"));
    std::string error;
    ASSERT_TRUE(
        an::tests::fixtures::writeAcquisitionFrame(darkFrame, std::filesystem::path(dark.toStdWString()), error))
        << error;
    m_manifest.calibrateLightsInMemory = true;
    std::vector<QString> lights;
    for (const double code : {10000.0, 20000.0, 30000.0})
    {
        lights.push_back(writeCodeLight(QStringLiteral("light_%1").arg(code, 0, 'f', 0), code));
        WorkflowManifest::LightsCalibrationParams params;
        params.lightPath = FrameId(lights.back());
        params.darkPath = FrameId(dark);
        params.settings.darkOptimization = true;
        m_manifest.lightsCalibration.push_back(params);
    }
    useUnregisteredLights(lights);

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), (20000.0 - 1000.0) / 65535.0, 1e-6)
        << "each light is calibrated with the fixed factor of one";
    const auto said = std::count_if(result.warnings.begin(), result.warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("could not be optimized"));
    });
    EXPECT_EQ(said, 1) << result.warnings.join(QStringLiteral(" | ")).toStdString();
}

// On the registration's plane a light calibrated in memory is not a number where its flat is zero, and the stack reads
// that sample as the plane's no-data zero: warped as the stack reads it, the group's matches are measured without it
// and the master there is the other light's sample. The group failed, because the stack measured its matches over every
// sample but an exact zero and the engine refused the sample.
TEST_F(LightsStackingTest, ALightCalibratedInMemoryAndWarpedAsItIsReadIsLeftOutWhereItsFlatIsZero)
{
    registerTranslatedPair(4.0, -3.0);
    calibrateReferenceByADeadFlat();
    updateGroup([](auto& group) {
        group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
        group.stacking.normalizationMode = NormalizationMode::Global;
    });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Transform-on-read")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    const double there = written->samples.value(kDeadX, kDeadY, 0u);
    EXPECT_TRUE(std::isfinite(there));
    EXPECT_GT(there, 0.0) << "the second light reaches the sample, so it is not the no-data marker";
}

// The registered light the alignment writes from a light calibrated in memory keeps the calibration's not-a-number
// where its flat is zero, and the stack of the registered images reads it as the plane's no-data zero: the group's
// matches are measured without it and the master there is the other light's sample. The group failed, because the
// stack measured its matches over every sample but an exact zero and the engine refused the sample.
TEST_F(LightsStackingTest, ARegisteredLightCalibratedInMemoryIsLeftOutWhereItsFlatIsZero)
{
    registerTranslatedPair(4.0, -3.0);
    calibrateReferenceByADeadFlat();
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    updateGroup([](auto& group) { group.stacking.normalizationMode = NormalizationMode::Global; });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Materialized")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    const double there = written->samples.value(kDeadX, kDeadY, 0u);
    EXPECT_TRUE(std::isfinite(there));
    EXPECT_GT(there, 0.0) << "the second light reaches the sample, so it is not the no-data marker";
}

TEST_F(LightsStackingTest, WeightsAreReadFromTheRegistrationsMetrics)
{
    // A registration that recorded every light where it is, with star counts that differ.
    const std::vector<QString> lights{writeFlatLight(QStringLiteral("a"), 0.2, 0.01, 61u),
                                      writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 62u),
                                      writeFlatLight(QStringLiteral("c"), 0.2, 0.01, 63u)};
    m_registration.groupKey = GroupKey{QStringLiteral("reg|64x48|Lum")};
    m_registration.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_integration.registrationGroupKey = m_registration.groupKey;
    m_integration.lightFiles = m_registration.lightFiles;
    m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
    m_integration.stacking.weighting = QStringLiteral("Number of Stars");
    m_integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};

    ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("StackingTestWriter"));
    ASSERT_TRUE(writer.initialize());
    an::ui::RegistrationGroupState state;
    state.groupKey = m_registration.groupKey.toString();
    state.settingsVersion = m_registration.settings.version;
    state.referenceFrameId = FrameId(lights[0]).canonicalPath();
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.lightFilesHash = QStringLiteral("lights");
    QString error;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    int stars = 20;
    for (const QString& path : lights)
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(path);
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.metrics.stars = stars;
        metric.metrics.eccentricity = 0.2;
        stars += 40;
        metric.transformJson = stage_utils::serializeTransformJson(an::RegistrationTransform{});
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
    }

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasWeightingStats);
    EXPECT_LT(quality.weightMin, quality.weightMax);
}

TEST_F(LightsStackingTest, ThePhotometricWeightsAreRaisedToTheGroupsFloor)
{
    // Three lights the registration measured photometrically. The third is the dimmest, so its raw transparency is one,
    // and its residual is four times the others', so its alignment term takes its raw weight far below the floor.
    const std::vector<QString> lights{writeFlatLight(QStringLiteral("a"), 0.2, 0.01, 71u),
                                      writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 72u),
                                      writeFlatLight(QStringLiteral("c"), 0.2, 0.01, 73u)};
    m_registration.groupKey = GroupKey{QStringLiteral("reg|64x48|Lum")};
    m_registration.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_integration.registrationGroupKey = m_registration.groupKey;
    m_integration.lightFiles = m_registration.lightFiles;
    m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
    m_integration.stacking.weighting = QStringLiteral("Photometric");
    m_integration.stacking.minWeightFloor = 0.5;
    m_integration.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable;
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};

    ASSERT_TRUE(QDir().mkpath(m_manifest.outputDirectory));
    an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("StackingTestWriter"));
    ASSERT_TRUE(writer.initialize());
    an::ui::RegistrationGroupState state;
    state.groupKey = m_registration.groupKey.toString();
    state.settingsVersion = m_registration.settings.version;
    state.referenceFrameId = FrameId(lights[0]).canonicalPath();
    state.detectionHash = QStringLiteral("detection");
    state.registrationHash = QStringLiteral("registration");
    state.lightFilesHash = QStringLiteral("lights");
    QString error;
    ASSERT_TRUE(writer.upsertGroupState(state, &error)) << error.toStdString();
    const std::vector<double> zeroPoints{24.0, 24.0, 21.0};
    const std::vector<double> residuals{1.0, 1.0, 4.0};
    std::vector<an::PhotometricFrameMeasurement> measurements;
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(lights[i]);
        metric.groupKey = state.groupKey;
        metric.settingsVersion = state.settingsVersion;
        metric.metrics.succeeded = true;
        metric.metrics.stars = 100;
        metric.metrics.photometric_zero_point = zeroPoints[i];
        metric.metrics.astrometric_rms_pixels = residuals[i];
        metric.transformJson = stage_utils::serializeTransformJson(an::RegistrationTransform{});
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();
        measurements.push_back({.zeroPointMagnitudes = zeroPoints[i],
                                .zeroPointStandardUncertaintyMagnitudes = std::nullopt,
                                .astrometricRootMeanSquarePixels = residuals[i]});
    }

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasWeightingStats);

    // The engine's photometric weights under the group's floor.
    an::Result<an::consumer::OperationScope> scope = m_run.session().tryAdmit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .maxConcurrency = 1u});
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    std::vector<an::PhotometricStackWeight> expected(measurements.size());
    const auto weighted = an::computePhotometricStackWeights(
        scope->execution(), measurements, an::PhotometricWeightingRequest{.minimumRawWeight = 0.5}, expected);
    ASSERT_TRUE(weighted.hasValue()) << weighted.status().message();
    EXPECT_NEAR(quality.weightMin, expected[2].value, 1e-6);
    EXPECT_NEAR(quality.weightMax, expected[0].value, 1e-6);
}

TEST_F(LightsStackingTest, AWeightingWithoutItsMetricsStacksUnweightedAndSaysSo)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.2, 0.01, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.2, 0.01, 3u)});
    updateGroup([](auto& group) { group.stacking.weighting = QStringLiteral("Number of Stars"); });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    EXPECT_FALSE(result.groupResults.front().qualityReport.hasWeightingStats);
}

// The width weighting penalizes a light for the stars it lacks against the reference - the first light by identity,
// as the group names none - so the hazy light with the narrowest stars is the lightest, not the heaviest. These are
// the weights the Frame Quality dashboard's preview and the report show for the same lights.
TEST_F(LightsStackingTest, WeightedFwhmPenalizesALightForTheStarsItLacksAgainstTheReference)
{
    const std::vector<int> stars{400, 100, 300};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    useMeasuredLights(QStringLiteral("Weighted FWHM"), [&](const std::size_t index, auto& metric) {
        metric.metrics.stars = stars[index];
        metric.metrics.fwhm = widths[index];
        metric.metrics.eccentricity = 0.2;
    });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasWeightingStats);
    const std::vector<double> expected =
        an::tests::integration::stackWidthWeights(widths, {400.0, 100.0, 300.0}, 0u, 0.01);
    EXPECT_NEAR(quality.weightMax, expected[0], 1e-9) << "the reference has the narrowest effective stars";
    EXPECT_NEAR(quality.weightMin, expected[1], 1e-9) << "the hazy light is raised to the floor";
    EXPECT_LT(expected[1], expected[2]);
}

// A light whose stars have no measured roundness - recorded by the registration as an eccentricity of one - is weighed
// with the others. Its record stated a roundness of zero, which the engine refuses in any light's record, and the
// group's stack failed whatever its weighting read.
TEST_F(LightsStackingTest, ALightWhoseStarsHaveNoMeasuredRoundnessIsWeighedWithTheOthers)
{
    useMeasuredLights(QStringLiteral("Number of Stars"), [](const std::size_t index, auto& metric) {
        metric.metrics.stars = 20 + 40 * static_cast<int>(index);
        metric.metrics.eccentricity = index == 1u ? 1.0 : 0.2;
    });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasWeightingStats);
    EXPECT_LT(quality.weightMin, quality.weightMax);
}

// What the engine says of a group's stack is said among the stage's warnings: a sum of lights no sample of which is
// positive is written as it is, and the engine says so. The group's stack was admitted with no one to tell, so the
// engine said nothing and the warning was lost.
TEST_F(LightsStackingTest, WhatTheEngineWarnsOfAGroupsStackIsSaid)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.0, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.0, 0.0, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.0, 0.0, 3u)});
    updateGroup([](auto& group) { group.stacking.stackMethod = QStringLiteral("Sum"); });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("Sum not normalized")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const auto said = std::count_if(result.warnings.begin(), result.warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("Sum not normalized"));
    });
    EXPECT_EQ(said, 1) << "a sentence the engine says is said once";
    EXPECT_TRUE(QFileInfo::exists(masterPath()));
}

TEST_F(LightsStackingTest, ADrizzledPairIsStackedOnItsCanvas)
{
    registerTranslatedPair(4.0, -3.0);
    updateGroup([](auto& group) {
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0f;
        settings.pixelFraction = 0.9f;
        group.drizzle = settings;
    });
    LightsDrizzleStage drizzle;
    ASSERT_TRUE(m_run.run(drizzle, m_manifest).isSuccess());

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Drizzle")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_GT(written->samples.width(), an::tests::integration::kStarFieldWidth * 3u / 2u);
    EXPECT_GT(written->samples.height(), an::tests::integration::kStarFieldHeight * 3u / 2u);
    ASSERT_TRUE(written->card("DRIZZLE").has_value());
    EXPECT_TRUE(writtenIdentity(m_run.session(), masterPath()).has_value());
}

// Lights with no area common to all of them have no intersection to be placed on. The alignment placed them on the
// standard canvas and said so, the stack that warps them as it reads them did so without a word, and the drizzle failed
// - and the master the first two stacked carried no world coordinates, because the registration had recorded none for
// an intersection it could not plan. Every route places them on the standard canvas, says so in the same words, and
// stacks a master that carries the reference's solution.
TEST_F(LightsStackingTest, LightsSharingNoAreaAreStackedOnTheStandardCanvasByEveryRouteWhichSaysSo)
{
    registerSidePanels();
    const QString fallback =
        QStringLiteral("so its intersection canvas is replaced by the standard reference-frame canvas");
    const auto said = [this, &fallback]() {
        const QStringList& warnings = m_run.loggedWarnings();
        return std::count_if(warnings.begin(), warnings.end(),
                             [&fallback](const QString& warning) { return warning.contains(fallback); });
    };
    const auto expectMaster = [this](std::uint32_t width, std::uint32_t height) {
        const auto written = readWrittenImage(masterPath());
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->samples.width(), width);
        EXPECT_EQ(written->samples.height(), height);
        QStringList said = m_run.loggedWarnings();
        said << m_run.loggedDetails().filter(QStringLiteral("WCS"));
        EXPECT_TRUE(written->card("CTYPE1").has_value())
            << "the master carries no world coordinates: " << said.join(QStringLiteral(" | ")).toStdString();
    };
    const std::uint32_t width = an::tests::integration::kStarFieldWidth;
    const std::uint32_t height = an::tests::integration::kStarFieldHeight;

    // Warped as the stack reads them.
    SCOPED_TRACE("transform-on-read");
    updateGroup([](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable; });
    std::ptrdiff_t before = said();
    StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Transform-on-read")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    EXPECT_EQ(said(), before + 1) << "the stack that warps the lights as it reads them did not say so once";
    expectMaster(width, height);

    // Aligned, then stacked as the registered images.
    SCOPED_TRACE("registered images");
    updateGroup([](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Retained; });
    m_manifest.reprocessExisting = true;
    before = said();
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    EXPECT_EQ(said(), before + 1) << "the alignment did not say so once";
    result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Materialized")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    expectMaster(width, height);

    // Drizzled.
    SCOPED_TRACE("drizzle");
    updateGroup([](auto& group) {
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0;
        settings.pixelFraction = 0.9;
        group.drizzle = settings;
    });
    before = said();
    LightsDrizzleStage drizzle;
    const StageResult drizzled = m_run.run(drizzle, m_manifest);
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
    EXPECT_EQ(said(), before + 1) << "the drizzle did not say so once";
    before = said();
    result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Drizzle")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    EXPECT_EQ(said(), before + 1) << "the drizzled stack did not say so once";
    expectMaster(2u * width, 2u * height);
}

// A drizzled master never carried the registration's world coordinates: the stack read the record of the
// registration's plane but not the registration's reference, so the record's check refused it. The master carries the
// plane's solution moved onto its drizzled canvas - here an intersection's, guarded at its edges and offset from the
// plane by a fraction of a pixel - and every star the master holds lies where its solution puts the sky the reference
// light's solution gives that star.
TEST_F(LightsStackingTest, ADrizzledMasterCarriesTheRegistrationsSolutionOnItsCanvas)
{
    registerTranslatedPair(5.8, -4.1, 0.05, an::tests::integration::tanSolutionCards(80.5, 60.5),
                           QStringLiteral("intersection"));
    updateGroup([](auto& group) {
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0;
        settings.pixelFraction = 0.9;
        group.drizzle = settings;
    });
    LightsDrizzleStage drizzle;
    const StageResult drizzled = m_run.run(drizzle, m_manifest);
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    std::optional<an::WcsSolution> reference;
    std::optional<an::WcsSolution> master;
    std::uint32_t masterWidth = 0u;
    std::uint32_t masterHeight = 0u;
    {
        an::Result<an::consumer::OperationScope> scope = m_run.session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        ASSERT_TRUE(scope.hasValue());
        const auto referenceFacts = stage_utils::probeRegistrationFrame(*scope, nullptr, m_referencePath);
        const auto masterFacts = stage_utils::probeRegistrationFrame(*scope, nullptr, masterPath());
        ASSERT_TRUE(referenceFacts.hasValue() && masterFacts.hasValue());
        reference = referenceFacts->solution;
        master = masterFacts->solution;
        masterWidth = masterFacts->width;
        masterHeight = masterFacts->height;
    }
    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(master.has_value()) << "the drizzled master carries no world coordinates: "
                                    << m_run.loggedWarnings().join(QStringLiteral(" | ")).toStdString();
    // Each star's centre on the master, measured over what rises above the sky near it, against where the master's
    // solution puts the star's sky; a solution moved by the wrong offset puts every star a pixel or more away.
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    const auto centreNear = [&written](const an::PixelCoordinate& at) -> std::optional<an::PixelCoordinate> {
        constexpr int kReach = 5;
        const int column = static_cast<int>(std::floor(at.x));
        const int row = static_cast<int>(std::floor(at.y));
        std::vector<double> rim;
        for (int v = row - kReach; v <= row + kReach; ++v)
        {
            for (int u = column - kReach; u <= column + kReach; ++u)
            {
                if ((std::abs(v - row) == kReach || std::abs(u - column) == kReach) && u >= 0 && v >= 0 &&
                    u < static_cast<int>(written->samples.width()) && v < static_cast<int>(written->samples.height()))
                {
                    const double value =
                        written->samples.value(static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v), 0u);
                    if (std::isfinite(value))
                    {
                        rim.push_back(value);
                    }
                }
            }
        }
        if (rim.empty())
        {
            return std::nullopt;
        }
        std::nth_element(rim.begin(), rim.begin() + static_cast<std::ptrdiff_t>(rim.size() / 2u), rim.end());
        const double sky = rim[rim.size() / 2u];
        double weight = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        for (int v = row - kReach + 1; v < row + kReach; ++v)
        {
            for (int u = column - kReach + 1; u < column + kReach; ++u)
            {
                if (u < 0 || v < 0 || u >= static_cast<int>(written->samples.width()) ||
                    v >= static_cast<int>(written->samples.height()))
                {
                    continue;
                }
                const double value =
                    written->samples.value(static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v), 0u);
                const double rise = std::isfinite(value) ? (std::max)(0.0, value - sky) : 0.0;
                weight += rise;
                sumX += rise * (static_cast<double>(u) + 0.5);
                sumY += rise * (static_cast<double>(v) + 0.5);
            }
        }
        if (!(weight > 0.0))
        {
            return std::nullopt;
        }
        return an::PixelCoordinate{.x = sumX / weight, .y = sumY / weight};
    };
    std::size_t checked = 0u;
    double meanX = 0.0;
    double meanY = 0.0;
    for (const FixtureStar& star : m_stars)
    {
        const auto sky = reference->pixelToWorld(an::PixelCoordinate{star.x + 0.5, star.y + 0.5});
        ASSERT_TRUE(sky.hasValue());
        const auto onMaster = master->worldToPixel(*sky);
        ASSERT_TRUE(onMaster.hasValue());
        if (onMaster->x < 10.0 || onMaster->y < 10.0 || onMaster->x > static_cast<double>(masterWidth) - 10.0 ||
            onMaster->y > static_cast<double>(masterHeight) - 10.0)
        {
            continue;
        }
        const std::optional<an::PixelCoordinate> centre = centreNear(*onMaster);
        ASSERT_TRUE(centre.has_value()) << "star at " << star.x << ", " << star.y;
        EXPECT_NEAR(centre->x, onMaster->x, 0.5) << "star at " << star.x << ", " << star.y;
        EXPECT_NEAR(centre->y, onMaster->y, 0.5) << "star at " << star.x << ", " << star.y;
        meanX += centre->x - onMaster->x;
        meanY += centre->y - onMaster->y;
        ++checked;
    }
    ASSERT_GE(checked, 10u);
    EXPECT_NEAR(meanX / static_cast<double>(checked), 0.0, 0.15);
    EXPECT_NEAR(meanY / static_cast<double>(checked), 0.0, 0.15);
}

// A drizzled master keeps its lights' level at every scale. The engine renders each drizzled frame at its lights'
// level divided by the scale squared, and the stage stacked those frames as they were, so a master drizzled at twice
// the scale held a quarter of its lights' level, and at three times a ninth.
TEST_F(LightsStackingTest, ADrizzledMasterKeepsItsLightsLevelAtEveryScale)
{
    registerTranslatedPair(4.0, -3.0);
    const std::optional<double> level = medianOf(m_referencePath);
    ASSERT_TRUE(level.has_value());
    for (const double scale : {1.0, 1.5, 2.0, 3.0})
    {
        SCOPED_TRACE(scale);
        updateGroup([scale](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = static_cast<float>(scale);
            settings.pixelFraction = 1.0f;
            group.drizzle = settings;
        });
        m_manifest.reprocessExisting = true;
        LightsDrizzleStage drizzle;
        const StageResult drizzled = m_run.run(drizzle, m_manifest);
        ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
        const StageResult result = stack();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        const std::optional<double> master = medianOf(masterPath());
        ASSERT_TRUE(master.has_value());
        EXPECT_NEAR(*master, *level, 0.05 * *level);
    }
}

// The normalization stage measures a drizzled group's coefficients on the canvas's analysis plane, at the lights' own
// level, and the stack applies them to the drizzled frames: at that level each light is brought to the reference. The
// frames were stacked at their lights' level divided by the scale squared, so an offset measured at the lights' level
// overshot it, and a light brighter than the reference pulled the master below zero.
TEST_F(LightsStackingTest, ADrizzledGroupsCoefficientsBringEachLightToTheReferenceAtItsLevel)
{
    // The second light's background is 0.05 above the reference's.
    registerTranslatedPair(4.0, -3.0, 0.10);
    updateGroup([](auto& group) {
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0f;
        settings.pixelFraction = 1.0f;
        group.drizzle = settings;
        group.stacking.normalizationMode = NormalizationMode::Global;
        group.stacking.globalNormalizationMethod = GlobalNormalizationMethod::Additive;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsNormalizationStage normalization;
    const StageResult normalized = m_run.run(normalization, m_manifest);
    ASSERT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
    LightsDrizzleStage drizzle;
    const StageResult drizzled = m_run.run(drizzle, m_manifest);
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const std::optional<double> reference = medianOf(m_referencePath);
    const std::optional<double> master = medianOf(masterPath());
    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(master.has_value());
    EXPECT_NEAR(*master, *reference, 0.05 * *reference);
}

// A drizzled Local group's stack applies the normalization stage's document, measured on the canvas's analysis plane
// under the group's reference strategy and every analysis setting: the run says the stage's coefficients brought the
// lights to the reference, and the settings it states name the strategy. The drizzled stack fitted its own surfaces on
// the drizzled lights, and the run said the strategy, the evaluation, the PSF flux scale and the filters did not apply.
TEST_F(LightsStackingTest, ADrizzledLocalGroupsStackAppliesTheNormalizationStagesDocument)
{
    registerTranslatedPair(4.0, -3.0, 0.10);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    drizzleWholeDrops(2.0f);
    const StageResult result = normalizeDrizzleAndStack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("normalization coefficients")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    const QString& applied = result.groupResults.front().qualityReport.normalization;
    EXPECT_TRUE(applied.contains(QStringLiteral("normalization stage's coefficients"))) << applied.toStdString();
    EXPECT_TRUE(applied.contains(QFileInfo(m_referencePath).fileName())) << applied.toStdString();
    EXPECT_FALSE(applied.contains(QStringLiteral("drizzled stack"))) << applied.toStdString();
    // The settings the run log states before the stack name the reference strategy, as any Local group's do.
    QString settings;
    for (const QString& detail : m_run.loggedDetails())
    {
        if (detail.contains(QStringLiteral("Settings: Stack:")))
        {
            settings = detail;
            break;
        }
    }
    ASSERT_FALSE(settings.isEmpty()) << m_run.loggedDetails().join(QStringLiteral("\n")).toStdString();
    EXPECT_TRUE(settings.contains(QStringLiteral("ref:"))) << settings.toStdString();
    EXPECT_FALSE(settings.contains(QStringLiteral("drizzled stack"))) << settings.toStdString();
}

// A drizzled Local group's stack reads the normalization stage's document as any group's stack does: one an earlier run
// measured under the Global normalization the group stated then is said to be under other settings, and the stack
// measures its own. The drizzled stack read no document for such a group and said nothing of it.
TEST_F(LightsStackingTest, ADrizzledLocalGroupSaysADocumentMeasuredUnderOtherSettingsIsNotApplied)
{
    registerTranslatedPair(4.0, -3.0, 0.10);
    drizzleWholeDrops(2.0f);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Global;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsNormalizationStage normalization;
    const StageResult measured = m_run.run(normalization, m_manifest);
    ASSERT_TRUE(measured.isSuccess()) << measured.error.toLogString().toStdString();
    ASSERT_TRUE(
        QFileInfo::exists(stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front())));

    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
    });
    LightsDrizzleStage drizzle;
    const StageResult drizzled = m_run.run(drizzle, m_manifest);
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("measured under settings other than the group's")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    const QString& applied = result.groupResults.front().qualityReport.normalization;
    EXPECT_TRUE(applied.contains(QStringLiteral("Measured by the stack"))) << applied.toStdString();
}

/// The columns and rows of the reference's plane both lights of a pair hold samples at, the second shifted onto it by
/// whole pixels: from the first index on, up to the end index.
struct BothLightsCover
{
    std::uint32_t firstColumn = 0u;
    std::uint32_t endColumn = 0u;
    std::uint32_t firstRow = 0u;
    std::uint32_t endRow = 0u;
};

[[nodiscard]] BothLightsCover bothLightsCover(const std::pair<double, double>& shift, const std::uint32_t width,
                                              const std::uint32_t height)
{
    const auto first = [](const double offset) { return static_cast<std::uint32_t>((std::max)(0.0, offset)); };
    const auto end = [](const double offset, const std::uint32_t extent) {
        return static_cast<std::uint32_t>((std::min)(static_cast<double>(extent), extent + offset));
    };
    return BothLightsCover{.firstColumn = first(shift.first),
                           .endColumn = end(shift.first, width),
                           .firstRow = first(shift.second),
                           .endRow = end(shift.second, height)};
}

// A drizzled Local group's stack applies the normalization stage's document where each drizzled light lies on the
// plane it was measured on. At a scale of one, with a whole drop, the Square kernel and a whole-pixel shift, each
// drizzled light holds its registered image's samples, so wherever both lights hold samples the drizzled master is
// the master the same lights make stacked as registered images, within float rounding. The drizzled stack fitted its
// own surfaces on the drizzled lights instead, and its master stood up to 0.05 from that one.
TEST_F(LightsStackingTest, ADrizzledLocalMasterAtScaleOneIsTheMasterOfTheRegisteredLights)
{
    std::pair<double, double> shift;
    ASSERT_NO_FATAL_FAILURE(registerWholePixelPair(shift));
    const std::optional<an::tests::fixtures::FixtureSamples> registered = stackRegisteredLocalPair();
    ASSERT_TRUE(registered.has_value());

    drizzleWholeDrops(1.0f);
    const StageResult result = normalizeDrizzleAndStack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("normalization coefficients")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    EXPECT_TRUE(result.groupResults.front().qualityReport.normalization.contains(
        QStringLiteral("normalization stage's coefficients")))
        << result.groupResults.front().qualityReport.normalization.toStdString();
    const auto drizzled = readWrittenImage(masterPath());
    ASSERT_TRUE(drizzled.has_value());
    ASSERT_EQ(drizzled->samples.width(), registered->width());
    ASSERT_EQ(drizzled->samples.height(), registered->height());
    const BothLightsCover covered = bothLightsCover(shift, registered->width(), registered->height());
    std::size_t compared = 0u;
    for (std::uint32_t y = covered.firstRow; y < covered.endRow; ++y)
    {
        for (std::uint32_t x = covered.firstColumn; x < covered.endColumn; ++x)
        {
            const double expected = registered->value(x, y, 0u);
            const double actual = drizzled->samples.value(x, y, 0u);
            ASSERT_TRUE(std::isfinite(expected) && std::isfinite(actual)) << x << ',' << y;
            EXPECT_NEAR(actual, expected, 4.0 * std::numeric_limits<float>::epsilon() * std::abs(expected))
                << x << ',' << y;
            ++compared;
        }
    }
    EXPECT_GT(compared, static_cast<std::size_t>(registered->width()) * registered->height() / 2u);
}

// At a scale of two each light drizzles onto blocks of two by two, and every surface is read a quarter pixel from the
// block's centre on the plane it was measured on: wherever both lights hold samples, the drizzled Local master's block
// means are the samples of the master the lights make stacked as registered images, within a thousandth of the
// background.
TEST_F(LightsStackingTest, ADrizzledLocalMastersBlocksAtScaleTwoAreTheMasterOfTheRegisteredLights)
{
    std::pair<double, double> shift;
    ASSERT_NO_FATAL_FAILURE(registerWholePixelPair(shift));
    const std::optional<an::tests::fixtures::FixtureSamples> registered = stackRegisteredLocalPair();
    ASSERT_TRUE(registered.has_value());
    const std::optional<double> background = medianOf(masterPath());
    ASSERT_TRUE(background.has_value());

    drizzleWholeDrops(2.0f);
    const StageResult result = normalizeDrizzleAndStack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto drizzled = readWrittenImage(masterPath());
    ASSERT_TRUE(drizzled.has_value());
    ASSERT_EQ(drizzled->samples.width(), 2u * registered->width());
    ASSERT_EQ(drizzled->samples.height(), 2u * registered->height());
    const BothLightsCover covered = bothLightsCover(shift, registered->width(), registered->height());
    std::size_t compared = 0u;
    double worst = 0.0;
    for (std::uint32_t y = covered.firstRow; y < covered.endRow; ++y)
    {
        for (std::uint32_t x = covered.firstColumn; x < covered.endColumn; ++x)
        {
            const double expected = registered->value(x, y, 0u);
            double sum = 0.0;
            for (std::uint32_t dy = 0u; dy < 2u; ++dy)
            {
                for (std::uint32_t dx = 0u; dx < 2u; ++dx)
                {
                    sum += drizzled->samples.value(2u * x + dx, 2u * y + dy, 0u);
                }
            }
            ASSERT_TRUE(std::isfinite(expected) && std::isfinite(sum)) << x << ',' << y;
            worst = (std::max)(worst, std::abs(sum / 4.0 - expected));
            ++compared;
        }
    }
    EXPECT_GT(compared, static_cast<std::size_t>(registered->width()) * registered->height() / 2u);
    EXPECT_LT(worst, 1.0e-3 * *background) << "background " << *background;
}

// A drizzled Local group is brought to the reference its local normalization ranks best, which only the normalization
// stage's analysis chooses: the master holds the brighter light's level. The drizzled stack fitted its own surfaces
// against the manifest's reference, whatever the strategy, and held the first light's.
TEST_F(LightsStackingTest, ADrizzledLocalGroupIsBroughtToTheReferenceItsAnalysisRanksBest)
{
    // The second light's background is 0.05 above the first's, so its median ranks it best; the manifest names the
    // first.
    registerTranslatedPair(4.0, -3.0, 0.10);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    drizzleWholeDrops(2.0f);
    const StageResult result = normalizeDrizzleAndStack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const ImageIntegration::NormalizationDocumentRead document = ImageIntegration::readNormalizationCoefficients(
        stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front()), std::nullopt);
    ASSERT_TRUE(document.coefficients.has_value()) << document.message.toStdString();
    ASSERT_TRUE(document.coefficients->local.has_value());
    EXPECT_EQ(document.coefficients->plane, ImageIntegration::NormalizationMeasurementPlane::DrizzleAnalysisPlane);
    ASSERT_EQ(document.coefficients->local->referenceIndex, std::optional<std::uint32_t>(1u))
        << "the document names the brighter light, which the manifest does not";
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    EXPECT_TRUE(result.groupResults.front().qualityReport.normalization.contains(QFileInfo(m_lightPath).fileName()))
        << result.groupResults.front().qualityReport.normalization.toStdString();
    // Both lights brought to the brighter one: the master holds its level, not the first light's.
    const std::optional<double> brighter = medianOf(m_lightPath);
    const std::optional<double> first = medianOf(m_referencePath);
    const std::optional<double> master = medianOf(masterPath());
    ASSERT_TRUE(brighter.has_value() && first.has_value() && master.has_value());
    EXPECT_NEAR(*master, *brighter, 0.05 * *brighter);
    EXPECT_GT(std::abs(*master - *first), 0.5 * std::abs(*brighter - *first));
}

// A local normalization that ranks the lights itself writes the light it ranks best as its document's reference, and
// the stack takes that light as its own reference, so the document is applied as it was measured. The stack kept the
// reference the manifest names, the engine refuses a document that names another, and the stack fitted its own
// surfaces against the manifest's reference instead, without the analysis the document held.
TEST_F(LightsStackingTest, ALocalDocumentsRankedReferenceIsTheLightTheStackBringsTheOthersTo)
{
    // The second light's background is 0.05 above the first's, so its median ranks it best; the manifest names the
    // first.
    registerTranslatedPair(4.0, -3.0, 0.10);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    LightsNormalizationStage normalization;
    const StageResult normalized = m_run.run(normalization, m_manifest);
    ASSERT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
    const ImageIntegration::NormalizationDocumentRead document = ImageIntegration::readNormalizationCoefficients(
        stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front()), std::nullopt);
    ASSERT_TRUE(document.coefficients.has_value()) << document.message.toStdString();
    ASSERT_TRUE(document.coefficients->local.has_value());
    ASSERT_EQ(document.coefficients->local->referenceIndex, std::optional<std::uint32_t>(1u))
        << "the document names the brighter light, which the manifest does not";

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    // Both lights brought to the brighter one: the master holds its level, not the first light's.
    const std::optional<double> reference = medianOf(m_lightPath);
    const std::optional<double> master = medianOf(masterPath());
    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(master.has_value());
    EXPECT_NEAR(*master, *reference, 0.05 * *reference);
    ASSERT_EQ(result.groupResults.size(), 1u);
    const QString& applied = result.groupResults.front().qualityReport.normalization;
    EXPECT_TRUE(applied.contains(QStringLiteral("normalization stage's coefficients"))) << applied.toStdString();
    EXPECT_TRUE(applied.contains(QFileInfo(m_lightPath).fileName())) << applied.toStdString();
}

// A local document normalizes the lights to the light it ranks best, which the engine reads from the stack's one
// reference; the lights are weighed against the group's reference all the same, as the Frame Quality dashboard and the
// report weigh them. The stack weighed every width against the document's light, so the three disagreed on the weights.
TEST_F(LightsStackingTest, TheLightsAreWeighedAgainstTheGroupsReferenceWhereALocalDocumentNamesAnother)
{
    m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
    m_registration.settings.autoReferenceDuringRegistration = false;
    m_registration.settings.enrichWithGaiaCatalog = false;
    m_registration.settings.transformMode = QStringLiteral("Linear");
    m_registration.settings.transformation = QStringLiteral("Affine");
    m_integration.registrationGroupKey = m_registration.groupKey;
    m_stars = starCatalogue(30u, 17u);
    // The third light's background is the brightest, so a median ranks it best; the manifest names the first.
    const std::vector<QString> lights{
        writeStarLight(QStringLiteral("light_001"), m_stars, 71u, 0.05),
        writeStarLight(QStringLiteral("light_002"),
                       movedStars(m_stars, [](double x, double y) { return std::pair{x + 3.0, y - 2.0}; }), 72u, 0.05),
        writeStarLight(QStringLiteral("light_003"),
                       movedStars(m_stars, [](double x, double y) { return std::pair{x - 2.0, y + 4.0}; }), 73u, 0.10)};
    m_registration.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
    m_registration.referenceFrame = m_registration.lightFiles.front();
    m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
    m_integration.lightFiles = m_registration.lightFiles;
    m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
    m_integration.normalizationReferenceFrames = {m_integration.lightFiles.front()};
    m_integration.stacking.weighting = QStringLiteral("Weighted FWHM");
    m_integration.stacking.minWeightFloor = 0.01;
    m_integration.stacking.normalizationMode = NormalizationMode::Local;
    m_integration.stacking.localNormalization.enabled = true;
    m_integration.stacking.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
    m_integration.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};
    LightsRegistrationStage registration;
    const StageResult registered = m_run.run(registration, m_manifest);
    ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();

    // The widths and star counts the stack weighs the lights by, in place of what the registration measured.
    const std::vector<double> stars{400.0, 100.0, 300.0};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    {
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("WeightsAgainstReader"));
        ASSERT_TRUE(loader.open());
        std::vector<an::ui::RegistrationMetricRecord> records;
        ASSERT_TRUE(
            loader.loadMetricRecords(m_registration.groupKey.toString(), m_registration.settings.version, records));
        ASSERT_EQ(records.size(), lights.size());
        an::ui::RegistrationMetricsWriter writer(metricsPath(), QStringLiteral("WeightsAgainstWriter"));
        ASSERT_TRUE(writer.openExisting());
        for (an::ui::RegistrationMetricRecord& record : records)
        {
            const auto index = static_cast<std::size_t>(
                std::distance(lights.begin(), std::find_if(lights.begin(), lights.end(), [&](const QString& light) {
                                  return FrameId(light) == record.frameId;
                              })));
            ASSERT_LT(index, lights.size());
            record.metrics.stars = static_cast<int>(stars[index]);
            record.metrics.fwhm = widths[index];
            record.metrics.eccentricity = 0.2;
            QString error;
            ASSERT_TRUE(writer.upsertMetric(record, &error)) << error.toStdString();
        }
    }

    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    LightsNormalizationStage normalization;
    const StageResult normalized = m_run.run(normalization, m_manifest);
    ASSERT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
    const ImageIntegration::NormalizationDocumentRead document = ImageIntegration::readNormalizationCoefficients(
        stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front()), std::nullopt);
    ASSERT_TRUE(document.coefficients.has_value()) << document.message.toStdString();
    ASSERT_TRUE(document.coefficients->local.has_value());
    ASSERT_EQ(document.coefficients->local->referenceIndex, std::optional<std::uint32_t>(2u))
        << "the document names the brightest light, which the manifest does not";

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    EXPECT_TRUE(quality.normalization.contains(QStringLiteral("normalization stage's coefficients")))
        << quality.normalization.toStdString();
    EXPECT_TRUE(quality.normalization.contains(QFileInfo(lights[2]).fileName())) << quality.normalization.toStdString();

    const std::vector<double> againstGroup = an::tests::integration::stackWidthWeights(widths, stars, 0u, 0.01);
    const std::vector<double> againstDocument = an::tests::integration::stackWidthWeights(widths, stars, 2u, 0.01);
    ASSERT_GT(std::abs(againstGroup[2] - againstDocument[2]), 0.05) << "the two references weigh the lights alike";
    ASSERT_EQ(quality.lightWeights.size(), lights.size());
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        EXPECT_EQ(FrameId(quality.lightWeights[index].light), FrameId(lights[index])) << index;
        EXPECT_NEAR(quality.lightWeights[index].weight, againstGroup[index], 1e-6) << index;
    }
}

// The default strategy integrates the best lights into a reference that is no light, and its document names none. The
// stack applies it over its own reference, every light brought to the integrated surface and its level matched to that
// light's. The engine matches the stack's reference by the identity, which no match against the integrated reference
// is, so it refused the document and the stack fitted its own surfaces against its reference alone.
TEST_F(LightsStackingTest, ALocalDocumentsIntegratedReferenceIsAppliedOverTheStacksReference)
{
    // Integrated, the two lights' backgrounds, 0.05 and 0.10, make a surface between them.
    registerTranslatedPair(4.0, -3.0, 0.10);
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Local;
        group.stacking.localNormalization.enabled = true;
        group.stacking.localNormalization.referenceStrategy = QStringLiteral("Integrate Best Frames");
        group.stacking.localNormalization.evaluationCriteria = QStringLiteral("Median");
        group.stacking.localNormalization.maxIntegratedFrames = 2;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    LightsNormalizationStage normalization;
    const StageResult normalized = m_run.run(normalization, m_manifest);
    ASSERT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const std::optional<double> first = medianOf(m_referencePath);
    const std::optional<double> second = medianOf(m_lightPath);
    const std::optional<double> master = medianOf(masterPath());
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(master.has_value());
    const double between = 0.5 * (*first + *second);
    EXPECT_NEAR(*master, between, 0.1 * between);
    ASSERT_EQ(result.groupResults.size(), 1u);
    const QString& applied = result.groupResults.front().qualityReport.normalization;
    EXPECT_TRUE(applied.contains(QStringLiteral("normalization stage's coefficients"))) << applied.toStdString();
    EXPECT_TRUE(applied.contains(QFileInfo(m_referencePath).fileName())) << applied.toStdString();
}

// The coefficients a drizzled group measured on its canvas's analysis plane apply at any drizzle scale, so a group
// whose scale alone changed applies the coefficients the normalization stage reuses for it. Both fingerprints called
// them current, and the stack refused them on every run as measured on another canvas.
TEST_F(LightsStackingTest, ADrizzledGroupsCoefficientsApplyAfterItsScaleAloneChanges)
{
    // The second light's background is 0.05 above the reference's.
    registerTranslatedPair(4.0, -3.0, 0.10);
    const auto drizzleAt = [this](const float scale) {
        updateGroup([scale](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = scale;
            settings.pixelFraction = 1.0f;
            group.drizzle = settings;
            group.stacking.normalizationMode = NormalizationMode::Global;
            group.stacking.globalNormalizationMethod = GlobalNormalizationMethod::Additive;
            group.normalizationReferenceFrames = {group.lightFiles.front()};
        });
        LightsNormalizationStage normalization;
        const StageResult normalized = m_run.run(normalization, m_manifest);
        ASSERT_TRUE(normalized.isSuccess()) << normalized.error.toLogString().toStdString();
        m_manifest.reprocessExisting = true;
        LightsDrizzleStage drizzle;
        const StageResult drizzled = m_run.run(drizzle, m_manifest);
        m_manifest.reprocessExisting = false;
        ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
    };
    drizzleAt(2.0f);
    const QString sidecar = ImageIntegration::normalizationSidecarPath(
        stage_utils::normalizationArtifactPath(m_manifest, m_manifest.integrationGroups.front()));
    QFile measured(sidecar);
    ASSERT_TRUE(measured.open(QIODevice::ReadOnly));
    const QByteArray atTwice = measured.readAll();
    measured.close();
    ASSERT_TRUE(stack().isSuccess());

    drizzleAt(3.0f);
    ASSERT_TRUE(measured.open(QIODevice::ReadOnly));
    EXPECT_EQ(measured.readAll(), atTwice) << "the coefficients are reused at the new scale";
    measured.close();
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const std::optional<double> reference = medianOf(m_referencePath);
    const std::optional<double> master = medianOf(masterPath());
    ASSERT_TRUE(reference.has_value());
    ASSERT_TRUE(master.has_value());
    EXPECT_NEAR(*master, *reference, 0.05 * *reference);
}

// The stack applies only coefficients measured under the settings it stacks with; a document measured under others is
// said, and the stack measures its own. The stack read the document with no identity, so coefficients a settings
// change left behind were applied whenever the stage that would replace them had not run.
TEST_F(LightsStackingTest, CoefficientsMeasuredUnderOtherSettingsAreNotApplied)
{
    const QString reference = writeFlatLight(QStringLiteral("a"), 0.1, 0.01, 7u);
    useUnregisteredLights({reference, writeFlatLight(QStringLiteral("b"), 0.25, 0.02, 7u)});
    updateGroup([](auto& group) {
        group.stacking.normalizationMode = NormalizationMode::Global;
        group.normalizationReferenceFrames = {group.lightFiles.front()};
    });
    LightsNormalizationStage normalization;
    ASSERT_TRUE(m_run.run(normalization, m_manifest).isSuccess());

    // A setting the coefficients' numbers cannot show: only their identity tells them apart.
    updateGroup([](auto& group) { group.stacking.overlapNormalization.enabled = true; });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("measured under settings other than the group's")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("measures its own")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    EXPECT_TRUE(
        result.groupResults.front().qualityReport.normalization.contains(QStringLiteral("Measured by the stack")))
        << result.groupResults.front().qualityReport.normalization.toStdString();
}

// A spatial re-stack leaves out the light its rejection flags and stacks the others with the same rejection; the master
// carries the group's reference light's cards, and the record names the light it left out by its place among the
// group's lights, where the report looks it up. The master took the cards of the light after the reference whenever a
// light before it was left out, because the reference was found by its place among the lights the first stack read;
// and the record named the left-out light by that place too.
TEST_F(LightsStackingTest, ASpatialRestackKeepsTheGroupsReferenceAndNamesTheLightItLeavesOut)
{
    std::vector<QString> lights;
    for (std::uint64_t index = 0; index < 6u; ++index)
    {
        // The second light carries a bright star no other light has, which the rejection takes out of every sample its
        // profile reaches, so the light stands out and is flagged.
        const std::vector<FixtureStar> star =
            index == 1u ? std::vector<FixtureStar>{FixtureStar{30.0, 24.0, 3.0, 0.7}} : std::vector<FixtureStar>{};
        const FixtureCard object{"OBJECT", "Field " + std::to_string(index), "",
                                 an::tests::fixtures::FixtureCardKind::Text};
        lights.push_back(writeFlatLight(QStringLiteral("l%1").arg(index), 0.2, 0.0, 81u + index, star, {object}));
    }
    // The group lists its lights in another order than the stack reads them, which nothing forbids.
    const QString flagged = lights[1];
    const QString reference = lights[3];
    std::reverse(lights.begin(), lights.end());
    useUnregisteredLights(lights);
    updateGroup([&reference](auto& group) {
        group.stacking.rejectionMethod = QStringLiteral("Sigma Clipping");
        group.stacking.sigmaLow = 1.5;
        group.stacking.sigmaHigh = 1.5;
        group.stacking.createRejectionMaps = true;
        group.stacking.mergeRejectionMaps = true;
        group.stacking.restackExcludingFlaggedFrames = true;
        group.normalizationReferenceFrames = {FrameId(reference)};
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    const IntegrationGroupResult& group = result.groupResults.front();
    const StackQualityRecord& quality = group.qualityReport;
    ASSERT_TRUE(quality.spatialRestackApplied) << result.warnings.join(QStringLiteral("\n")).toStdString();
    EXPECT_EQ(group.lightFrameCount, 5);
    const auto listed = std::find(lights.begin(), lights.end(), flagged);
    ASSERT_TRUE(listed != lights.end());
    EXPECT_EQ(quality.spatialRestackExcludedFrameIndices,
              (std::vector<std::uint64_t>{static_cast<std::uint64_t>(std::distance(lights.begin(), listed))}))
        << "the left-out light is named by its place among the group's lights";
    // The re-stack of five lights still rejects, so its map is written.
    EXPECT_EQ(group.rejectionMaps.size(), 1u);

    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(30u, 24u, 0u), 0.2, 1e-6) << "the flagged light's star is left out";
    ASSERT_TRUE(written->card("OBJECT").has_value());
    EXPECT_NE(written->card("OBJECT")->find("Field 3"), std::string::npos)
        << "the master carries another light's cards: " << *written->card("OBJECT");
    ASSERT_TRUE(written->card("TOTEXP").has_value());
    EXPECT_DOUBLE_EQ(std::stod(*written->card("TOTEXP")), 300.0);
}

// A bucket of an exposure merge holding one light is read as a stack of it would read it: where a calibration left the
// light not a number - its flat zero there - the bucket holds no sample, so below the highlight the merge keeps the
// longer exposure there as everywhere. The bucket was read as it was, so the not-a-number reached the merged master.
TEST_F(LightsStackingTest, AnExposureMergeBucketOfOneLightLeavesOutWhereItsFlatIsZero)
{
    const QString longFirst = writeCodeLight(QStringLiteral("long1"), 10000.0);
    const QString longSecond = writeCodeLight(QStringLiteral("long2"), 10000.0);
    const QString shortLight = writeCodeLight(QStringLiteral("short1"), 3000.0);
    // The dead sample lies outside the central third the flat's level is read from, so both flats divide by one.
    const QString dead = writeFlatMaster(QStringLiteral("flat_dead"), kLightWidth, kLightHeight,
                                         {an::tests::fixtures::FixtureSampleOverride{.x = 2u, .y = 3u, .value = 0.0}});
    const QString flat = writeFlatMaster(QStringLiteral("flat"), kLightWidth, kLightHeight);
    calibrateInMemoryBy(longFirst, flat);
    calibrateInMemoryBy(longSecond, flat);
    calibrateInMemoryBy(shortLight, dead);
    useUnregisteredLights({longFirst, longSecond, shortLight});
    updateGroup([](auto& group) {
        group.lightExposureSeconds = {60.0, 60.0, 15.0};
        group.stacking.hdr.enabled = true;
    });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 10000.0 / 65535.0, 1e-6);
    const double there = written->samples.value(2u, 3u, 0u);
    EXPECT_TRUE(std::isfinite(there)) << "the shorter bucket's not-a-number reached the master";
    EXPECT_NEAR(there, 10000.0 / 65535.0, 1e-6) << "below the highlight the longer exposure is kept";
}

// The exposure merge blends only samples: where the longer exposure holds none - a calibration left it not a number -
// the shorter one, brought to its scale, is the master, and where the shorter holds none the longer stays, however
// bright. A bucket's empty sample was blended as a zero, so the master was zero in both places.
TEST_F(LightsStackingTest, AnExposureMergeBlendsOnlyTheSamplesEachExposureHolds)
{
    // The dead sample lies outside the central third the flat's level is read from, so both flats divide by one.
    const QString dead = writeFlatMaster(QStringLiteral("flat_dead"), kLightWidth, kLightHeight,
                                         {an::tests::fixtures::FixtureSampleOverride{.x = 2u, .y = 3u, .value = 0.0}});
    const QString flat = writeFlatMaster(QStringLiteral("flat"), kLightWidth, kLightHeight);
    const double saturated = 64000.0 / 65535.0;
    const double shorter = 15000.0 / 65535.0;

    // The longer exposure, past the highlight, holds no sample at the dead one: the shorter is the master there too.
    const QString longDead = writeCodeLight(QStringLiteral("long_dead"), 64000.0);
    const QString shortFirst = writeCodeLight(QStringLiteral("short_a"), 15000.0);
    const QString shortSecond = writeCodeLight(QStringLiteral("short_b"), 15000.0);
    calibrateInMemoryBy(longDead, dead);
    calibrateInMemoryBy(shortFirst, flat);
    calibrateInMemoryBy(shortSecond, flat);
    useUnregisteredLights({longDead, shortFirst, shortSecond});
    updateGroup([](auto& group) {
        group.lightExposureSeconds = {60.0, 15.0, 15.0};
        group.stacking.hdr.enabled = true;
    });
    const StageResult filled = stack();
    ASSERT_TRUE(filled.isSuccess()) << filled.error.toLogString().toStdString();
    auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 4.0 * shorter, 1e-5);
    EXPECT_NEAR(written->samples.value(2u, 3u, 0u), 4.0 * shorter, 1e-5)
        << "where the longer exposure holds no sample the shorter one is the master";

    // The shorter exposure holds no sample at the dead one: the saturated longer exposure stays there.
    m_manifest.lightsCalibration.clear();
    const QString longFirst = writeCodeLight(QStringLiteral("long_a"), 64000.0);
    const QString longSecond = writeCodeLight(QStringLiteral("long_b"), 64000.0);
    const QString shortDead = writeCodeLight(QStringLiteral("short_dead"), 15000.0);
    calibrateInMemoryBy(longFirst, flat);
    calibrateInMemoryBy(longSecond, flat);
    calibrateInMemoryBy(shortDead, dead);
    useUnregisteredLights({longFirst, longSecond, shortDead});
    updateGroup([](auto& group) { group.lightExposureSeconds = {60.0, 60.0, 15.0}; });
    const StageResult kept = stack();
    ASSERT_TRUE(kept.isSuccess()) << kept.error.toLogString().toStdString();
    written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 4.0 * shorter, 1e-5);
    EXPECT_NEAR(written->samples.value(2u, 3u, 0u), saturated, 1e-5)
        << "where the shorter exposure holds no sample the longer one stays";
}

// A per-pixel weighting stored with a grid finer than the engine takes - a hand-edited or earlier session; the dialog
// offers none - runs at the finest grid the engine takes and says so. The engine refused the request and the group
// failed.
TEST_F(LightsStackingTest, APerPixelWeightingGridFinerThanTheEngineTakesRunsAtItsFinestAndIsSaid)
{
    const auto count = [](const char* frames) {
        return FixtureCard{"NCOMBINE", frames, "", an::tests::fixtures::FixtureCardKind::Integer};
    };
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.2, 0.02, 1u, {}, {count("2")}),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.02, 2u, {}, {count("4")}),
                           writeFlatLight(QStringLiteral("c"), 0.2, 0.02, 3u, {}, {count("8")})});
    updateGroup([](auto& group) {
        group.stacking.weighting = QStringLiteral("Number of images");
        group.stacking.localWeighting.enabled = true;
        group.stacking.localWeighting.gridSpacing = 2;
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("grid spacing 2 as 4")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasLocalWeightingStats);
    EXPECT_EQ(quality.localWeightGridSpacing, 4);
}

// "Number of images" weighs each light by how many frames its header says it combines - STACKCNT, which the stack
// writes on every master, or the NCOMBINE another program writes - so masters of 10, 20 and 40
// frames are combined in that proportion, and the master states the 70 frames beneath it. Every light was weighed one,
// the weights were said to be computed, and no master stated a count.
TEST_F(LightsStackingTest, NumberOfImagesWeighsEachLightByTheFramesItsHeaderSaysItCombines)
{
    const auto count = [](const char* keyword, const char* frames) {
        return FixtureCard{keyword, frames, "", an::tests::fixtures::FixtureCardKind::Integer};
    };
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u, {}, {count("NCOMBINE", "10")}),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u, {}, {count("NCOMBINE", "20")}),
                           writeFlatLight(QStringLiteral("c"), 0.4, 0.0, 3u, {}, {count("STACKCNT", "40")})});
    updateGroup([](auto& group) { group.stacking.weighting = QStringLiteral("Number of images"); });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(anyContains(result.warnings, QStringLiteral("unweighted")))
        << result.warnings.join(QStringLiteral("\n")).toStdString();
    const StackQualityRecord& quality = result.groupResults.front().qualityReport;
    ASSERT_TRUE(quality.hasWeightingStats);
    const double mean = 70.0 / 3.0;
    EXPECT_NEAR(quality.weightMin, 10.0 / mean, 1e-6);
    EXPECT_NEAR(quality.weightMax, 40.0 / mean, 1e-6);

    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), (10.0 * 0.1 + 20.0 * 0.2 + 40.0 * 0.4) / 70.0, 1e-6);
    ASSERT_TRUE(written->card("STACKCNT").has_value());
    EXPECT_EQ(std::stoi(*written->card("STACKCNT")), 70);
}

// Lights that state no count above one leave "Number of images" nothing to weigh by: the group is stacked unweighted
// and says so once, and the master states the lights it combines. The stack weighed every light one, said the weights
// were computed and wrote a weight-sum map that held the coverage.
TEST_F(LightsStackingTest, NumberOfImagesOverLightsThatStateNoCountIsSaidOnceAndStacksUnweighted)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.4, 0.0, 3u)});
    updateGroup([](auto& group) {
        group.stacking.weighting = QStringLiteral("Number of images");
        group.stacking.createQualityMaps = true;
    });
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto said = std::count_if(result.warnings.begin(), result.warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("STACKCNT")) && warning.contains(QStringLiteral("unweighted"));
    });
    EXPECT_EQ(said, 1) << result.warnings.join(QStringLiteral("\n")).toStdString();
    const IntegrationGroupResult& group = result.groupResults.front();
    EXPECT_FALSE(group.qualityReport.hasWeightingStats);
    EXPECT_FALSE(group.qualityMaps.empty());
    for (const QualityMapResult& map : group.qualityMaps)
    {
        EXPECT_FALSE(map.path.contains(QStringLiteral("weightsum"))) << map.path.toStdString();
    }
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_NEAR(written->samples.value(10u, 10u, 0u), (0.1 + 0.2 + 0.4) / 3.0, 1e-6);
    ASSERT_TRUE(written->card("STACKCNT").has_value());
    EXPECT_EQ(std::stoi(*written->card("STACKCNT")), 3);
}

// A filter-array drizzle of lights whose headers state their pattern drizzles each sample into its colour, and the
// master is a colour image. The stack asked the drizzle for one output plane - the lights' own - which the engine
// refuses for a filter array it routes into three, so no filter-array drizzle was ever stacked.
TEST_F(LightsStackingTest, AFilterArrayDrizzleStacksTheLightsInTheColoursTheirHeadersState)
{
    registerTranslatedPair(4.0, -3.0, 0.05, {}, QString(), true);
    updateGroup([](auto& group) {
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0;
        settings.pixelFraction = 0.9;
        settings.cfaAware = true;
        settings.cfaPattern = QStringLiteral("Auto");
        group.drizzle = settings;
    });
    LightsDrizzleStage drizzle;
    const StageResult drizzled = m_run.run(drizzle, m_manifest);
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->samples.channelCount(), 3u);
    EXPECT_GT(written->samples.width(), an::tests::integration::kStarFieldWidth * 3u / 2u);
}

// A filter-array drizzle whose settings state a pattern reads each light under it - the registration's own rule for a
// stated pattern - so lights whose headers carry no pattern card are drizzled in colour; with the pattern left to the
// headers such a light is refused, naming the card it lacks. The stated pattern was read by nothing: such lights were
// refused, and the refusal did not say why.
TEST_F(LightsStackingTest, AFilterArrayDrizzleReadsLightsWithoutAPatternCardUnderTheStatedPattern)
{
    registerTranslatedPair(4.0, -3.0);
    const auto drizzleUnder = [this](const QString& pattern) {
        updateGroup([&pattern](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = 2.0;
            settings.pixelFraction = 0.9;
            settings.cfaAware = true;
            settings.cfaPattern = pattern;
            group.drizzle = settings;
        });
        LightsDrizzleStage drizzle;
        return m_run.run(drizzle, m_manifest);
    };

    // The drizzle stage refuses the group and goes on with the others, so its refusal is in the stage's report.
    const StageResult refused = drizzleUnder(QStringLiteral("Auto"));
    ASSERT_TRUE(refused.isPartialSuccess());
    ASSERT_EQ(refused.errorReport.errors.size(), 1u);
    EXPECT_TRUE(refused.errorReport.errors.front().toLogString().contains(QStringLiteral("BAYERPAT")))
        << refused.errorReport.errors.front().toLogString().toStdString();

    m_manifest.reprocessExisting = true;
    const StageResult drizzled = drizzleUnder(QStringLiteral("RGGB"));
    ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->samples.channelCount(), 3u);
}

// The group's result says where the master's solution came from - carried from the registration's plane, solved, or
// none and why - which the report prints beside the master. Nothing kept it: the stage said it in the run log alone.
TEST_F(LightsStackingTest, TheGroupResultSaysWhereTheMastersSolutionCameFrom)
{
    registerTranslatedPair(4.0, -3.0, 0.05, an::tests::integration::tanSolutionCards(80.5, 60.5));
    updateGroup([](auto& group) { group.stacking.registeredOutputPolicy = RegisteredOutputPolicy::Discardable; });
    const StageResult registered = stack();
    ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    ASSERT_EQ(registered.groupResults.size(), 1u);
    EXPECT_EQ(registered.groupResults.front().masterSolution, MasterSolutionOutcome::Propagated);
    EXPECT_TRUE(registered.groupResults.front().masterSolutionDetail.contains(QStringLiteral("registration")))
        << registered.groupResults.front().masterSolutionDetail.toStdString();

    m_integration.registrationGroupKey = GroupKey{};
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    const StageResult unsolved = stack();
    ASSERT_TRUE(unsolved.isSuccess()) << unsolved.error.toLogString().toStdString();
    ASSERT_EQ(unsolved.groupResults.size(), 1u);
    EXPECT_EQ(unsolved.groupResults.front().masterSolution, MasterSolutionOutcome::None);
    EXPECT_TRUE(
        unsolved.groupResults.front().masterSolutionDetail.contains(QStringLiteral("plate solving is disabled")))
        << unsolved.groupResults.front().masterSolutionDetail.toStdString();
}

// A group that asks for a fallback solve has its master solved when the registration carries it no solution, and the
// group's result keeps what the solve came to: with no solver configured, that none is; with a solver that finds
// nothing - the internal solver over a catalog holding no star - that the solve failed. Either way the master is
// published, the run admitted again after the solve, without a solution.
TEST_F(LightsStackingTest, AFallbackSolveIsTriedForAMasterWithNoSolutionToCarryAndWhatItCameToIsKept)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u)});
    updateGroup([](auto& group) { group.stacking.solveFinalMasterIfNeeded = true; });
    QTemporaryDir catalog;
    ASSERT_TRUE(catalog.isValid());
    an::tests::integration::ScopedProcessWindowContext installed;
    const QString noCatalog = QDir(m_root.path()).filePath(QStringLiteral("no_catalog"));
    QString unconfigured;
    for (const bool solver : {false, true})
    {
        SCOPED_TRACE(solver ? "a solver that finds nothing" : "no solver");
        if (solver)
        {
            ASSERT_TRUE(an::tests::integration::configureEmptyGaiaSolver(installed.context(), catalog.path()));
        }
        m_manifest.plateSolve.gaiaCatalogDirectory = solver ? catalog.path() : noCatalog;
        ASSERT_TRUE(QFile::remove(masterPath()) || !QFileInfo::exists(masterPath()));
        const qsizetype since = m_run.loggedDetails().size();

        const StageResult result = stack();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        ASSERT_EQ(result.groupResults.size(), 1u);
        const IntegrationGroupResult& group = result.groupResults.front();
        EXPECT_EQ(group.masterSolution, MasterSolutionOutcome::None);
        const QStringList details = m_run.loggedDetails().mid(since);
        EXPECT_TRUE(anyContains(details, QStringLiteral("Resolving fallback WCS")))
            << details.join(QStringLiteral(" | ")).toStdString();
        if (solver)
        {
            EXPECT_TRUE(group.masterSolutionDetail.contains(QStringLiteral("fallback solve failed")))
                << group.masterSolutionDetail.toStdString();
            EXPECT_NE(group.masterSolutionDetail, unconfigured);
        }
        else
        {
            unconfigured = group.masterSolutionDetail;
            EXPECT_TRUE(group.masterSolutionDetail.contains(QStringLiteral("no plate solver is configured")))
                << group.masterSolutionDetail.toStdString();
        }
        EXPECT_TRUE(anyContains(result.warnings, group.masterSolutionDetail))
            << result.warnings.join(QStringLiteral(" | ")).toStdString();
        const auto written = readWrittenImage(masterPath());
        ASSERT_TRUE(written.has_value());
        EXPECT_FALSE(written->card("CTYPE1").has_value()) << "the master carries a solution nothing found";
        EXPECT_NEAR(written->samples.value(10u, 10u, 0u), 0.15, 1e-6);
    }
}

/// The samples of a written image's first plane, read by the neutral reader, row by row.
[[nodiscard]] std::vector<double> samplesOf(const QString& path)
{
    const auto written = readWrittenImage(path);
    std::vector<double> values;
    if (!written.has_value())
    {
        return values;
    }
    for (std::uint32_t y = 0; y < written->samples.height(); ++y)
    {
        for (std::uint32_t x = 0; x < written->samples.width(); ++x)
        {
            values.push_back(written->samples.value(x, y, 0u));
        }
    }
    return values;
}

/// The mean of a written image's first plane over the columns `[first, last)`, read by the neutral reader.
[[nodiscard]] std::optional<double> meanOverColumns(const QString& path, std::uint32_t first, std::uint32_t last)
{
    const auto written = readWrittenImage(path);
    if (!written.has_value() || last > written->samples.width() || first >= last)
    {
        return std::nullopt;
    }
    double sum = 0.0;
    for (std::uint32_t y = 0; y < written->samples.height(); ++y)
    {
        for (std::uint32_t x = first; x < last; ++x)
        {
            sum += written->samples.value(x, y, 0u);
        }
    }
    return sum / (static_cast<double>(last - first) * written->samples.height());
}

// The per-pixel weighting weighs each light where it is measured: a light brighter and far noisier than the others
// over its left half alone is weighed down there and nowhere else, so the master's left half keeps the clean lights'
// level, which the frame weighting alone - every light's noise the same - averages the bright light into.
TEST_F(LightsStackingTest, APerPixelWeightingWeighsALightDownOnlyWhereItIsNoisy)
{
    const std::vector<QString> lights =
        useMeasuredLights(QStringLiteral("Noise"), [](const std::size_t, an::ui::RegistrationMetricRecord& metric) {
            metric.metrics.stars = 100;
            metric.noise.background_noise = 0.01;
            metric.noise.global_scale = 0.01;
        });
    {
        an::tests::fixtures::FixtureImage image;
        image.name = "c";
        image.width = kLightWidth;
        image.height = kLightHeight;
        image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
        image.pattern = an::tests::fixtures::FixturePattern::Noise;
        image.seed = 93u;
        image.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 0.2, .amplitude = 0.01}};
        std::string error;
        auto samples = an::tests::fixtures::materializeFixture(image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        an::tests::fixtures::FixtureImage noisy = image;
        noisy.seed = 94u;
        noisy.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 0.3, .amplitude = 0.1}};
        const auto noisySamples = an::tests::fixtures::materializeFixture(noisy, error);
        ASSERT_TRUE(noisySamples.has_value()) << error;
        for (std::uint32_t y = 0; y < kLightHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kLightWidth / 2u; ++x)
            {
                samples->setValue(x, y, 0u, noisySamples->value(x, y, 0u));
            }
        }
        ASSERT_TRUE(an::tests::fixtures::writeFixtureFits(image, *samples,
                                                          std::filesystem::path(lights[2].toStdWString()), error))
            << error;
    }
    const auto stackWith = [this](const bool perPixel) {
        updateGroup([perPixel](auto& group) {
            group.stacking.localWeighting.enabled = perPixel;
            group.stacking.localWeighting.windowSize = 7;
            group.stacking.localWeighting.gridSpacing = 8;
        });
        const StageResult result = stack();
        EXPECT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        return std::pair{meanOverColumns(masterPath(), 0u, 24u), meanOverColumns(masterPath(), 40u, kLightWidth)};
    };

    const auto [frameLeft, frameRight] = stackWith(false);
    ASSERT_TRUE(frameLeft.has_value() && frameRight.has_value());
    EXPECT_NEAR(*frameLeft, (0.2 + 0.2 + 0.3) / 3.0, 0.005) << "every light weighs the same without the surface";
    EXPECT_NEAR(*frameRight, 0.2, 0.002);

    const auto [pixelLeft, pixelRight] = stackWith(true);
    ASSERT_TRUE(pixelLeft.has_value() && pixelRight.has_value());
    EXPECT_LT(*pixelLeft, 0.2 + 0.015) << "the noisy light was not weighed down where it is noisy";
    EXPECT_NEAR(*pixelRight, 0.2, 0.002);
}

/// The path of the map of one kind the group's result names beside its master.
[[nodiscard]] std::optional<QString> mapOfKind(const IntegrationGroupResult& group, const QString& master,
                                               const ImageIntegration::StackingAuxiliaryOutputKind kind)
{
    const QString filename = ImageIntegration::stackingAuxiliaryOutputForOutput(QFileInfo(master), kind).filename;
    for (const QualityMapResult& map : group.qualityMaps)
    {
        if (QFileInfo(map.path).fileName() == filename)
        {
            return map.path;
        }
    }
    return std::nullopt;
}

// The quality maps hold what the stack combined at each pixel: the coverage counts the lights holding a number there,
// the variance is those lights' sample variance, and an unweighted stack's signal-to-noise gain is that count. A light
// calibrated in memory by a flat that is zero at one pixel holds none there. No weight sum is written for a stack
// that weighs no light.
TEST_F(LightsStackingTest, TheQualityMapsHoldWhatTheStackCombinedAtEachPixel)
{
    const QString first = writeCodeLight(QStringLiteral("a"), 10000.0);
    const QString second = writeCodeLight(QStringLiteral("b"), 20000.0);
    const QString third = writeCodeLight(QStringLiteral("c"), 30000.0);
    const QString dead = writeFlatMaster(QStringLiteral("flat_dead"), kLightWidth, kLightHeight,
                                         {an::tests::fixtures::FixtureSampleOverride{.x = 2u, .y = 3u, .value = 0.0}});
    const QString flat = writeFlatMaster(QStringLiteral("flat"), kLightWidth, kLightHeight);
    calibrateInMemoryBy(first, dead);
    calibrateInMemoryBy(second, flat);
    calibrateInMemoryBy(third, flat);
    useUnregisteredLights({first, second, third});
    updateGroup([](auto& group) { group.stacking.createQualityMaps = true; });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const IntegrationGroupResult& group = result.groupResults.front();
    using Kind = ImageIntegration::StackingAuxiliaryOutputKind;
    const auto coverage = mapOfKind(group, masterPath(), Kind::Coverage);
    const auto variance = mapOfKind(group, masterPath(), Kind::Variance);
    const auto gain = mapOfKind(group, masterPath(), Kind::SnrGain);
    ASSERT_TRUE(coverage.has_value() && variance.has_value() && gain.has_value());
    EXPECT_FALSE(mapOfKind(group, masterPath(), Kind::WeightSum).has_value());
    EXPECT_FALSE(QFileInfo::exists(
        QDir(m_manifest.outputDirectory)
            .filePath(ImageIntegration::stackingAuxiliaryOutputForOutput(QFileInfo(masterPath()), Kind::WeightSum)
                          .filename)));

    const auto coverageMap = readWrittenImage(*coverage);
    const auto varianceMap = readWrittenImage(*variance);
    const auto gainMap = readWrittenImage(*gain);
    ASSERT_TRUE(coverageMap.has_value() && varianceMap.has_value() && gainMap.has_value());
    // The lights are a step of 10000 codes apart: three hold a variance of one step squared, the two the dead pixel
    // leaves half of it.
    const double step = 10000.0 / 65535.0;
    EXPECT_EQ(coverageMap->samples.value(10u, 10u, 0u), 3.0);
    EXPECT_EQ(coverageMap->samples.value(2u, 3u, 0u), 2.0);
    EXPECT_NEAR(varianceMap->samples.value(10u, 10u, 0u), step * step, 1e-6);
    EXPECT_NEAR(varianceMap->samples.value(2u, 3u, 0u), step * step / 2.0, 1e-6);
    EXPECT_NEAR(gainMap->samples.value(10u, 10u, 0u), 3.0, 1e-6);
    EXPECT_NEAR(gainMap->samples.value(2u, 3u, 0u), 2.0, 1e-6);
}

// A weighted stack's maps hold the weights it applied: the weight sum is the lights' weights summed - three, as the
// stack states its weights over their mean - and the signal-to-noise gain is that sum squared over the sum of the
// squared weights, the weights the width weighting gives the lights.
TEST_F(LightsStackingTest, AWeightedStacksMapsHoldTheWeightsItApplied)
{
    const std::vector<int> stars{400, 100, 300};
    const std::vector<double> widths{3.0, 2.0, 2.5};
    useMeasuredLights(QStringLiteral("Weighted FWHM"), [&](const std::size_t index, auto& metric) {
        metric.metrics.stars = stars[index];
        metric.metrics.fwhm = widths[index];
        metric.metrics.eccentricity = 0.2;
    });
    updateGroup([](auto& group) { group.stacking.createQualityMaps = true; });

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const IntegrationGroupResult& group = result.groupResults.front();
    using Kind = ImageIntegration::StackingAuxiliaryOutputKind;
    const auto weightSum = mapOfKind(group, masterPath(), Kind::WeightSum);
    const auto gain = mapOfKind(group, masterPath(), Kind::SnrGain);
    const auto coverage = mapOfKind(group, masterPath(), Kind::Coverage);
    ASSERT_TRUE(weightSum.has_value() && gain.has_value() && coverage.has_value());

    const std::vector<double> weights =
        an::tests::integration::stackWidthWeights(widths, {400.0, 100.0, 300.0}, 0u, 0.01);
    double sum = 0.0;
    double squares = 0.0;
    for (const double weight : weights)
    {
        sum += weight;
        squares += weight * weight;
    }
    const auto weightMap = readWrittenImage(*weightSum);
    const auto gainMap = readWrittenImage(*gain);
    const auto coverageMap = readWrittenImage(*coverage);
    ASSERT_TRUE(weightMap.has_value() && gainMap.has_value() && coverageMap.has_value());
    for (const auto& [x, y] : {std::pair{10u, 10u}, std::pair{40u, 30u}})
    {
        EXPECT_NEAR(weightMap->samples.value(x, y, 0u), sum, 1e-5);
        EXPECT_NEAR(gainMap->samples.value(x, y, 0u), sum * sum / squares, 1e-5);
        EXPECT_EQ(coverageMap->samples.value(x, y, 0u), 3.0);
    }
    EXPECT_LT(sum * sum / squares, 3.0) << "the weights differ, so the gain is below the count";
}

// Every drizzle kernel the dialog offers is the one the lights are drizzled with: over a pair turned by a degree and
// a half, which separates the axis-aligned turbo drop from the square's exact overlap, each kernel's master keeps the
// lights' level, names its kernel by its key, and differs from the master of every other kernel.
TEST_F(LightsStackingTest, EveryDrizzleKernelIsTheOneTheLightsAreDrizzledWith)
{
    const double turn = 1.5 * 3.14159265358979323846 / 180.0;
    const double cx = an::tests::integration::kStarFieldWidth / 2.0;
    const double cy = an::tests::integration::kStarFieldHeight / 2.0;
    registerMovedPair([turn, cx, cy](double x, double y) {
        const double u = x - cx;
        const double v = y - cy;
        return std::pair{cx + std::cos(turn) * u - std::sin(turn) * v + 2.4,
                         cy + std::sin(turn) * u + std::cos(turn) * v - 1.6};
    });
    const std::optional<double> level = medianOf(m_referencePath);
    ASSERT_TRUE(level.has_value());

    std::vector<std::pair<QString, std::vector<double>>> masters;
    for (const QString& kernel : {QStringLiteral("Square"), QStringLiteral("Point"), QStringLiteral("Turbo"),
                                  QStringLiteral("Gaussian"), QStringLiteral("Lanczos2"), QStringLiteral("Lanczos3")})
    {
        SCOPED_TRACE(kernel.toStdString());
        updateGroup([&kernel](auto& group) {
            WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
            settings.enabled = true;
            settings.scale = 1.0;
            settings.pixelFraction = 1.0;
            settings.kernel = kernel;
            group.drizzle = settings;
        });
        m_manifest.reprocessExisting = true;
        LightsDrizzleStage drizzle;
        const StageResult drizzled = m_run.run(drizzle, m_manifest);
        ASSERT_TRUE(drizzled.isSuccess()) << drizzled.error.toLogString().toStdString();
        ASSERT_FALSE(drizzled.isPartialSuccess()) << drizzled.errorReport.errors.front().toLogString().toStdString();
        const StageResult result = stack();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

        const auto written = readWrittenImage(masterPath());
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->card("DRIZKERN").value_or(""), kernel.toLower().toStdString());
        const std::optional<double> master = medianOf(masterPath());
        ASSERT_TRUE(master.has_value());
        EXPECT_NEAR(*master, *level, 0.05 * *level);
        const std::vector<double> samples = samplesOf(masterPath());
        for (const auto& [other, otherSamples] : masters)
        {
            ASSERT_EQ(otherSamples.size(), samples.size()) << other.toStdString();
            double largest = 0.0;
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                if (std::isfinite(samples[index]) && std::isfinite(otherSamples[index]))
                {
                    largest = (std::max)(largest, std::abs(samples[index] - otherSamples[index]));
                }
            }
            EXPECT_GT(largest, 1e-4) << "the master is " << other.toStdString() << "'s";
        }
        masters.emplace_back(kernel, samples);
    }
}

/// Where the comet's nucleus is in light `index`, in the neutral layer's coordinates.
[[nodiscard]] std::pair<double, double> cometNucleus(int index)
{
    return {60.0 + 3.0 * index, 50.0 + 1.5 * index};
}

class CometStackingTest : public LightsStackingTest
{
protected:
    void SetUp() override
    {
        LightsStackingTest::SetUp();
        // A comet run's directory is named by its whole run identity, which a long temporary root would push past the
        // Windows path limit; the outputs go below a short root of their own.
        ASSERT_TRUE(m_shortRoot.isValid());
        m_manifest.outputDirectory = QDir(m_shortRoot.path()).filePath(QStringLiteral("output"));
    }

    /// Three lights of a star field and a moving comet, observed in the first and the last, registered on the comet
    /// and aligned; the field drifts by `drift` pixels to the right from one light to the next. `beforeAlignment`, when
    /// given, is handed the lights once they are registered and before they are aligned.
    void alignOnTheComet(const QString& mode = QStringLiteral("Comet"), const double drift = 0.0,
                         const std::function<void(const std::vector<QString>&)>& beforeAlignment = {})
    {
        m_registration.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_registration.settings.autoReferenceDuringRegistration = false;
        m_registration.settings.enrichWithGaiaCatalog = false;
        m_registration.settings.transformMode = QStringLiteral("Linear");
        m_registration.settings.transformation = QStringLiteral("Affine");
        m_integration.registrationGroupKey = m_registration.groupKey;
        std::vector<FixtureStar> field;
        for (const FixtureStar& star : starCatalogue(30u, 29u))
        {
            if (std::hypot(star.x - 63.0, star.y - 51.5) > 14.0)
            {
                field.push_back(star);
            }
        }
        m_field = field;
        RegistrationSettings::CometTrackingSettings comet;
        comet.trackingOptions.searchRadiusPixels = 8.0;
        std::vector<QString> lights;
        for (int index = 0; index < 3; ++index)
        {
            std::vector<FixtureStar> stars =
                movedStars(field, [drift, index](double x, double y) { return std::pair{x + drift * index, y}; });
            stars.push_back(FixtureStar{cometNucleus(index).first, cometNucleus(index).second, 3.0, 0.8});
            const std::string when = "2026-03-01T01:" + std::to_string(index) + "0:00";
            const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/light_00%1.fits").arg(index + 1));
            std::string error;
            ASSERT_TRUE(writeStarField(
                path, stars,
                {FixtureCard{"EXPTIME", "60", {}, an::tests::fixtures::FixtureCardKind::Real}, exposureStartCard(when)},
                111u + static_cast<std::uint64_t>(index), error))
                << error;
            lights.push_back(path);
            m_registration.lightCaptureTimestampsUtc.push_back(QString::fromStdString(when));
            m_registration.lightExposureSeconds.push_back(60.0);
        }
        for (const int index : {0, 2})
        {
            RegistrationSettings::CometTrackingObservation observation;
            observation.frameId = FrameId(lights[static_cast<std::size_t>(index)]).canonicalPath();
            observation.sourcePosition = QPointF(cometNucleus(index).first + 0.5, cometNucleus(index).second + 0.5);
            comet.observations.push_back(observation);
        }
        m_registration.settings.registrationMethod = QStringLiteral("comet_tracking");
        m_registration.settings.cometTracking = comet;
        m_integration.stacking.cometStackingMode = mode;
        m_registration.lightFiles = {FrameId(lights[0]), FrameId(lights[1]), FrameId(lights[2])};
        m_registration.referenceFrame = m_registration.lightFiles.front();
        m_registration.settings.referenceImage = m_registration.lightFiles.front().canonicalPath();
        m_integration.lightFiles = m_registration.lightFiles;
        m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
        m_manifest.registrationGroups = {m_registration};
        m_manifest.integrationGroups = {m_integration};

        LightsRegistrationStage registration;
        const StageResult registered = m_run.run(registration, m_manifest);
        ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
        if (beforeAlignment)
        {
            beforeAlignment(lights);
        }
        LightsAlignmentStage alignment;
        const StageResult aligned = m_run.run(alignment, m_manifest);
        ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();
    }

    /// Aligns on the comet under a canvas mode, stacks the stars and the comet and blends them, and holds the composite
    /// to the reference light: the field's stars where they are and the nucleus where the reference light holds it,
    /// both moved by where the reference light's first pixel lies on the stars' canvas - the canvas's width beyond the
    /// reference's when the field drifts right, since the union then ends at the reference's right edge.
    void expectACompositeOnTheReferencesPlane(const QString& alignmentMode, const double drift,
                                              const double smallestShift)
    {
        m_registration.settings.alignmentMode = alignmentMode;
        alignOnTheComet(ImageIntegration::starsPlusCometExperimentalStackingModeToken(), drift);
        const StageResult result = stack();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString() << "\n"
                                        << m_run.loggedWarnings().join(QLatin1Char('\n')).toStdString();
        EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Stars + comet")))
            << result.groupResults.front().registrationInputPolicy.toStdString();

        const auto masterStamp = writtenIdentity(m_run.session(), masterPath());
        ASSERT_TRUE(masterStamp.has_value());
        for (const QString& layer : {QStringLiteral("comet_layer"), QStringLiteral("comet_mask")})
        {
            const QString path = ImageIntegration::detail::compositeLayerPath(masterPath(), layer);
            const auto stamp = writtenIdentity(m_run.session(), path);
            ASSERT_TRUE(stamp.has_value()) << path.toStdString();
            EXPECT_EQ(stamp->artifactId, masterStamp->artifactId) << path.toStdString();
        }

        const auto written = readWrittenImage(masterPath());
        ASSERT_TRUE(written.has_value());
        const double shift = static_cast<double>(written->samples.width()) -
                             static_cast<double>(an::tests::integration::kStarFieldWidth);
        EXPECT_GE(shift, smallestShift);
        EXPECT_LE(shift, smallestShift + 1.0);
        const auto nucleus = brightestNear(masterPath(), cometNucleus(0).first + shift, cometNucleus(0).second, 4);
        ASSERT_TRUE(nucleus.has_value());
        EXPECT_LE(std::abs(static_cast<double>(nucleus->x) - (cometNucleus(0).first + shift)), 1.0);
        EXPECT_LE(std::abs(static_cast<double>(nucleus->y) - cometNucleus(0).second), 1.0);
        const FixtureStar& star = m_field.front();
        const auto fieldStar = brightestNear(masterPath(), star.x + shift, star.y, 3);
        ASSERT_TRUE(fieldStar.has_value());
        EXPECT_LE(std::abs(static_cast<double>(fieldStar->x) - (star.x + shift)), 1.0);
        EXPECT_LE(std::abs(static_cast<double>(fieldStar->y) - star.y), 1.0);
    }

    QTemporaryDir m_shortRoot{QDir(QDir::tempPath()).filePath(QStringLiteral("s-XXXXXX"))};
    std::vector<FixtureStar> m_field;
};

TEST_F(CometStackingTest, ACometStackHoldsTheNucleusStillAndCarriesNoStarSolution)
{
    alignOnTheComet();
    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Comet-aligned")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    const auto brightest = brightestNear(masterPath(), cometNucleus(0).first, cometNucleus(0).second, 4);
    ASSERT_TRUE(brightest.has_value());
    EXPECT_LE(std::abs(static_cast<double>(brightest->x) - cometNucleus(0).first), 1.0);
    EXPECT_LE(std::abs(static_cast<double>(brightest->y) - cometNucleus(0).second), 1.0);
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    EXPECT_FALSE(written->card("CTYPE1").has_value());
}

TEST_F(CometStackingTest, AStarsPlusCometGroupBlendsTheCometStackIntoTheStars)
{
    expectACompositeOnTheReferencesPlane(QStringLiteral("standard"), 0.0, 0.0);
}

TEST_F(CometStackingTest, AMosaicCompositeBlendsTheCometStackAtTheStarCanvasOrigin)
{
    // The field drifts two pixels right per light, so the stars' union starts four pixels left of the reference light,
    // and a blend that ignored the canvas origin would put the comet stack four pixels beside the nucleus.
    expectACompositeOnTheReferencesPlane(QStringLiteral("mosaic"), 2.0, 4.0);
}

// A light calibrated in memory is not a number where its flat is zero, and the registered and comet-aligned images the
// alignment writes from it keep that sample; the composite splits each with that pixel left out, as it leaves out where
// the light does not reach, so the stars there are the other lights' mean. The group failed, because the split read the
// pixel as one the light reaches and refused the sample that is not a number.
TEST_F(CometStackingTest, ALightCalibratedInMemoryIsLeftOutOfTheCompositeWhereItsFlatIsZero)
{
    // The lights are registered as they were written and calibrated only for the alignment, so the composite is what
    // reads the sample that is not a number.
    m_registration.settings.alignmentMode = QStringLiteral("standard");
    alignOnTheComet(ImageIntegration::starsPlusCometExperimentalStackingModeToken(), 0.0,
                    [this](const std::vector<QString>& lights) {
                        const QString dead = writeFlatMaster(
                            QStringLiteral("flat_dead"), an::tests::integration::kStarFieldWidth,
                            an::tests::integration::kStarFieldHeight,
                            {an::tests::fixtures::FixtureSampleOverride{.x = kDeadX, .y = kDeadY, .value = 0.0}});
                        const QString flat =
                            writeFlatMaster(QStringLiteral("flat"), an::tests::integration::kStarFieldWidth,
                                            an::tests::integration::kStarFieldHeight);
                        calibrateInMemoryBy(lights[0], dead);
                        calibrateInMemoryBy(lights[1], flat);
                        calibrateInMemoryBy(lights[2], flat);
                    });

    const QString registered =
        QDir(m_manifest.outputDirectory)
            .filePath(QStringLiteral("registered_lights/%1/light_001_reg.fits")
                          .arg(ImageIntegration::sanitizeFilename(m_registration.groupKey.toString())));
    const auto base = readWrittenImage(registered);
    ASSERT_TRUE(base.has_value()) << registered.toStdString();
    bool holdsNotANumber = false;
    for (std::uint32_t y = 0u; y < base->samples.height() && !holdsNotANumber; ++y)
    {
        for (std::uint32_t x = 0u; x < base->samples.width() && !holdsNotANumber; ++x)
        {
            holdsNotANumber = std::isnan(base->samples.value(x, y, 0u));
        }
    }
    ASSERT_TRUE(holdsNotANumber) << "the reference's registered image keeps the calibration's sample that is not a "
                                    "number, which the composite reads";

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.groupResults.front().registrationInputPolicy.startsWith(QStringLiteral("Stars + comet")))
        << result.groupResults.front().registrationInputPolicy.toStdString();
    const auto written = readWrittenImage(masterPath());
    ASSERT_TRUE(written.has_value());
    const double there = written->samples.value(kDeadX, kDeadY, 0u);
    EXPECT_TRUE(std::isfinite(there));
    EXPECT_GT(there, 0.0) << "the other two lights reach the sample, so it is not the no-data marker";
}

TEST_F(CometStackingTest, AStarsPlusCometGroupIsNotDrizzled)
{
    alignOnTheComet();
    updateGroup([](auto& group) {
        group.stacking.cometStackingMode = ImageIntegration::starsPlusCometExperimentalStackingModeToken();
        WorkflowManifest::IntegrationGroup::DrizzleSettings settings;
        settings.enabled = true;
        settings.scale = 2.0f;
        group.drizzle = settings;
    });
    const StageResult result = stack();
    EXPECT_TRUE(result.isFailure());
    EXPECT_TRUE(result.error.toLogString().contains(QStringLiteral("does not drizzle")))
        << result.error.toLogString().toStdString();
    EXPECT_FALSE(QFileInfo::exists(masterPath()));
}

// A group whose master is still what the run would make is not stacked again: it leaves no result, and the stage
// names it among the groups it reused, with the master it left as it is.
TEST_F(LightsStackingTest, AGroupWhoseOutputIsValidIsNamedAsReusedAndHasNoResult)
{
    useUnregisteredLights({writeFlatLight(QStringLiteral("a"), 0.1, 0.0, 1u),
                           writeFlatLight(QStringLiteral("b"), 0.2, 0.0, 2u),
                           writeFlatLight(QStringLiteral("c"), 0.3, 0.0, 3u)});
    updateGroup([](auto& group) { group.skipIfExists = true; });

    const StageResult made = stack();
    ASSERT_TRUE(made.isSuccess()) << made.error.toLogString().toStdString();
    EXPECT_EQ(made.groupResults.size(), 1u);
    EXPECT_TRUE(made.reusedGroups.empty()) << "a master the run made is not one it reused";

    const StageResult reused = stack();
    ASSERT_TRUE(reused.isSuccess()) << reused.error.toLogString().toStdString();
    EXPECT_TRUE(reused.groupResults.empty());
    ASSERT_EQ(reused.reusedGroups.size(), 1u);
    EXPECT_EQ(reused.reusedGroups.front().groupKey, m_integration.groupKey.toString());
    EXPECT_EQ(QDir::cleanPath(reused.reusedGroups.front().outputPath), QDir::cleanPath(masterPath()));

    // A run told to stack everything again reuses nothing.
    m_manifest.reprocessExisting = true;
    const StageResult again = stack();
    ASSERT_TRUE(again.isSuccess()) << again.error.toLogString().toStdString();
    EXPECT_EQ(again.groupResults.size(), 1u);
    EXPECT_TRUE(again.reusedGroups.empty());
}

// A light the alignment wrote no registered image of is left out of the stack: the group's result names it with the
// reason, and the stage's warning still says so in its own words.
TEST_F(LightsStackingTest, ALightWithNoRegisteredOutputIsNamedAmongTheLightsTheStackDropped)
{
    registerTranslatedPair(4.0, -3.0);
    const QString third =
        writeStarLight(QStringLiteral("light_003"),
                       movedStars(m_stars, [](double x, double y) { return std::pair{x - 2.0, y + 5.0}; }), 73u);
    m_registration.lightFiles.push_back(FrameId(third));
    m_integration.lightFiles = m_registration.lightFiles;
    m_integration.lightExposureSeconds = {60.0, 60.0, 60.0};
    m_manifest.registrationGroups = {m_registration};
    m_manifest.integrationGroups = {m_integration};
    LightsRegistrationStage registration;
    const StageResult registered = m_run.run(registration, m_manifest);
    ASSERT_TRUE(registered.isSuccess()) << registered.error.toLogString().toStdString();
    ASSERT_FALSE(registered.isPartialSuccess()) << "the third light was left unregistered";
    LightsAlignmentStage alignment;
    const StageResult aligned = m_run.run(alignment, m_manifest);
    ASSERT_TRUE(aligned.isSuccess()) << aligned.error.toLogString().toStdString();

    // Every light has its registered image: the stack drops none.
    const StageResult whole = stack();
    ASSERT_TRUE(whole.isSuccess()) << whole.error.toLogString().toStdString();
    ASSERT_EQ(whole.groupResults.size(), 1u);
    EXPECT_EQ(whole.groupResults.front().lightFrameCount, 3);
    EXPECT_TRUE(whole.groupResults.front().droppedLights.empty());

    const QString registeredThird =
        QDir(m_manifest.outputDirectory)
            .filePath(QStringLiteral("registered_lights/%1/light_003_reg.fits")
                          .arg(ImageIntegration::sanitizeFilename(m_registration.groupKey.toString())));
    ASSERT_TRUE(QFileInfo::exists(registeredThird));
    ASSERT_TRUE(QFile::remove(registeredThird));
    m_manifest.reprocessExisting = true;

    const StageResult result = stack();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    const IntegrationGroupResult& group = result.groupResults.front();
    EXPECT_EQ(group.lightFrameCount, 2);
    ASSERT_EQ(group.droppedLights.size(), 1u);
    EXPECT_EQ(FrameId(group.droppedLights.front().light).identityKey(), FrameId(third).identityKey());
    EXPECT_EQ(group.droppedLights.front().reason, DroppedLightReason::NoRegisteredOutput);
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("Skipping 1 light frame(s): ")))
        << result.warnings.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(anyContains(result.warnings, QStringLiteral("(no registered output)")))
        << result.warnings.join(QLatin1Char('\n')).toStdString();
    // The stack's own record of the lights it combined does not name the light either.
    for (const StackLightWeight& weight : group.qualityReport.lightWeights)
    {
        EXPECT_NE(FrameId(weight.light).identityKey(), FrameId(third).identityKey());
    }
}

// The detail is the master's centre at full scale: 960 by 540 pixels of a master larger than that, and the whole of a
// smaller one, which is its own detail.
TEST_F(LightsStackingTest, TheResultCarriesADetailOfTheMastersCentreBesideItsWholeFramePreview)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.01, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 2u)});
    const StageResult small = stack();
    ASSERT_TRUE(small.isSuccess()) << small.error.toLogString().toStdString();
    ASSERT_EQ(small.groupResults.size(), 1u);
    const IntegrationGroupResult& smallGroup = small.groupResults.front();
    const QSize own(static_cast<int>(kLightWidth), static_cast<int>(kLightHeight));
    EXPECT_EQ(smallGroup.reportPreviewSize, own);
    EXPECT_EQ(smallGroup.reportDetailSize, own);
    EXPECT_EQ(QImage::fromData(smallGroup.reportDetailJpeg, "JPEG").size(), own);
    // The same pixels through the same stretch: a small master's detail is its whole-frame preview.
    EXPECT_EQ(smallGroup.reportDetailJpeg, smallGroup.reportPreviewJpeg);

    constexpr std::uint32_t kWide = 1100u;
    constexpr std::uint32_t kTall = 600u;
    m_integration.outputFilename = QStringLiteral("master_Large.fits");
    useUnregisteredLights({writeFlatLight(QStringLiteral("large_a"), 0.1, 0.01, 11u, {}, {}, kWide, kTall),
                           writeFlatLight(QStringLiteral("large_b"), 0.2, 0.01, 12u, {}, {}, kWide, kTall)});
    const StageResult large = stack();
    ASSERT_TRUE(large.isSuccess()) << large.error.toLogString().toStdString();
    ASSERT_EQ(large.groupResults.size(), 1u);
    const IntegrationGroupResult& largeGroup = large.groupResults.front();
    EXPECT_EQ(largeGroup.reportPreviewSize, QSize(static_cast<int>(kWide), static_cast<int>(kTall)));
    EXPECT_EQ(largeGroup.reportDetailSize, QSize(960, 540));
    EXPECT_EQ(QImage::fromData(largeGroup.reportDetailJpeg, "JPEG").size(), QSize(960, 540));
}

TEST_F(LightsStackingTest, AMasterWhosePreviewCannotBeMadeHasNoDetail)
{
    useUnregisteredLights(
        {writeFlatLight(QStringLiteral("a"), 0.1, 0.01, 1u), writeFlatLight(QStringLiteral("b"), 0.2, 0.01, 2u)});
    LightsStackingStage failing(
        {}, {}, [](const an::consumer::OperationScope&, const an::Image<float>&, const QString&) -> StackPreviewAssets {
            throw std::runtime_error("the preview could not be made");
        });
    const StageResult result = m_run.run(failing, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    ASSERT_EQ(result.groupResults.size(), 1u);
    EXPECT_TRUE(result.groupResults.front().success) << "a master is made whether or not its pictures are";
    EXPECT_TRUE(result.groupResults.front().reportPreviewJpeg.isEmpty());
    EXPECT_TRUE(result.groupResults.front().reportDetailJpeg.isEmpty());
    EXPECT_FALSE(result.groupResults.front().reportDetailSize.isValid());

    // A generator that gives a detail and no whole frame gives nothing the result keeps: a detail is never alone.
    m_manifest.reprocessExisting = true;
    LightsStackingStage detailOnly(
        {}, {}, [](const an::consumer::OperationScope&, const an::Image<float>&, const QString&) -> StackPreviewAssets {
            StackPreviewAssets assets;
            assets.reportDetailJpeg = QByteArrayLiteral("not a picture");
            assets.reportDetailSize = QSize(8, 8);
            return assets;
        });
    const StageResult alone = m_run.run(detailOnly, m_manifest);
    ASSERT_TRUE(alone.isSuccess()) << alone.error.toLogString().toStdString();
    ASSERT_EQ(alone.groupResults.size(), 1u);
    EXPECT_TRUE(alone.groupResults.front().reportDetailJpeg.isEmpty());
}

} // namespace
