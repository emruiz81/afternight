// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights registration stage: lights read through the run's registry, their stars detected and
/// each light registered onto its group's reference in engine pixel coordinates - by star matching with an affine, a
/// projective or a polynomial model, by phase correlation, by the person's control points - and recovered from the
/// frames' own solutions, distortion included, where nothing else registered them; a light that shares a third of its
/// stars with the reference registered by them; a light the reference holds too few stars of registered through a light
/// already registered near it; a light calibrated in memory searched without the sample its flat leaves not a number;
/// every record kept for the alignment that follows; and a light read in colour under its own filter array, an X-Trans
/// one included. A detection value the detector refuses runs at the nearest it takes, a reference with fewer stars
/// than the minimum lowers it no further than a match is accepted on, a stated polynomial order a light's pairs cannot
/// carry leaves it the linear transform its stars matched, and each is said; a group with no reference chosen names
/// the first light it is registered onto. A chromatic aberration correction over lights of one channel writes no copy
/// of them, and a correction that moves nothing says so, each once; Gaia enrichment where no catalog is installed is
/// said once by a group it would have enriched; and the plate solve a registration allows is the one its dialog shows.

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"
#include "Test_ImageIntegration/workflow/CalibrationRun.h"
#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "controls/settings/SettingsKeys.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureStar;
using an::tests::integration::CalibrationRun;
using an::tests::integration::exposureStartCard;
using an::tests::integration::movedStars;
using an::tests::integration::starCatalogue;
using an::tests::integration::tanSolutionCards;
using an::tests::integration::writeMosaicStarField;
using an::tests::integration::writeStarField;

using Motion = std::function<std::pair<double, double>(double, double)>;

constexpr double kCentreX = 80.0;
constexpr double kCentreY = 60.0;

/// A star's position in engine pixel coordinates, whose first pixel's centre is at (0.5, 0.5).
[[nodiscard]] an::PixelCoordinate enginePointOf(const FixtureStar& star) noexcept
{
    return an::PixelCoordinate{star.x + 0.5, star.y + 0.5};
}

[[nodiscard]] Motion translation(double dx, double dy)
{
    return [dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; };
}

/// A small rotation and scale about the frame's centre, then a translation.
[[nodiscard]] Motion affineMotion()
{
    return [](double x, double y) {
        constexpr double kAngle = 0.6 * 3.14159265358979323846 / 180.0;
        constexpr double kScale = 1.004;
        const double u = x - kCentreX;
        const double v = y - kCentreY;
        return std::pair{kCentreX + kScale * (std::cos(kAngle) * u - std::sin(kAngle) * v) + 2.3,
                         kCentreY + kScale * (std::sin(kAngle) * u + std::cos(kAngle) * v) - 1.7};
    };
}

/// The affine motion seen through a slight perspective.
[[nodiscard]] Motion projectiveMotion()
{
    return [](double x, double y) {
        const auto [ax, ay] = affineMotion()(x, y);
        const double w = 1.0 + 4.0e-5 * (x - kCentreX);
        return std::pair{kCentreX + (ax - kCentreX) / w, kCentreY + (ay - kCentreY) / w};
    };
}

/// A translation and a second-order bend no linear model holds.
[[nodiscard]] Motion bentMotion()
{
    return [](double x, double y) {
        const double u = x - kCentreX;
        const double v = y - kCentreY;
        return std::pair{x + 1.5 + 2.0e-4 * u * u, y - 0.8 + 2.0e-4 * v * v};
    };
}

/// Where a light looks onto a sky wider than it: the offset of the sky point its centre sees from the light's own
/// centre, and how far the light is turned about its centre. A sky star at `s` lies at `c + R (s - c - offset)` in the
/// light, `c` its centre and `R` the turn.
struct SkyWindow
{
    double offsetX = 0.0;
    double offsetY = 0.0;
    double degrees = 0.0;
};

/// The least distance a star a light holds keeps from its edges, so that no edge cuts a star.
constexpr double kWindowEdge = 8.0;

/// The sky stars a light holds - their indices into the sky - and where it holds them.
struct WindowStars
{
    std::vector<std::size_t> indices;
    std::vector<FixtureStar> stars;
};

/// A sky two star fields wide: one field, and another beside it, 134 pixels further along.
[[nodiscard]] std::vector<FixtureStar> wideSky()
{
    std::vector<FixtureStar> sky = starCatalogue(40u, 31u);
    for (FixtureStar star : starCatalogue(40u, 32u))
    {
        star.x += 134.0;
        sky.push_back(star);
    }
    return sky;
}

/// The stars of a sky a light looking onto it through a window holds.
[[nodiscard]] WindowStars windowStars(const std::vector<FixtureStar>& sky, const SkyWindow& window)
{
    const double angle = window.degrees * 3.14159265358979323846 / 180.0;
    const double right = static_cast<double>(an::tests::integration::kStarFieldWidth) - 1.0 - kWindowEdge;
    const double bottom = static_cast<double>(an::tests::integration::kStarFieldHeight) - 1.0 - kWindowEdge;
    WindowStars held;
    for (std::size_t index = 0; index < sky.size(); ++index)
    {
        const double u = sky[index].x - kCentreX - window.offsetX;
        const double v = sky[index].y - kCentreY - window.offsetY;
        FixtureStar star = sky[index];
        star.x = kCentreX + std::cos(angle) * u - std::sin(angle) * v;
        star.y = kCentreY + std::sin(angle) * u + std::cos(angle) * v;
        if (star.x >= kWindowEdge && star.x <= right && star.y >= kWindowEdge && star.y <= bottom)
        {
            held.indices.push_back(index);
            held.stars.push_back(star);
        }
    }
    return held;
}

/// How many sky stars two lights both hold.
[[nodiscard]] std::size_t sharedStars(const WindowStars& first, const WindowStars& second)
{
    return static_cast<std::size_t>(
        std::count_if(first.indices.begin(), first.indices.end(), [&second](std::size_t index) {
            return std::find(second.indices.begin(), second.indices.end(), index) != second.indices.end();
        }));
}

/// The sky stars a light holds, where the reference - the window with no offset and no turn - puts them.
[[nodiscard]] std::vector<FixtureStar> onTheReference(const std::vector<FixtureStar>& sky, const WindowStars& held)
{
    std::vector<FixtureStar> placed;
    placed.reserve(held.indices.size());
    for (const std::size_t index : held.indices)
    {
        placed.push_back(sky[index]);
    }
    return placed;
}

/// The transform each registered light was recorded with, keyed by its canonical path.
using RecordedTransforms = std::map<QString, stage_utils::ParsedTransformRecord>;

class LightsRegistrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
        ASSERT_TRUE(QDir().mkpath(QDir(m_root.path()).filePath(QStringLiteral("lights"))));
        m_manifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("output"));
        m_manifest.executionId = QStringLiteral("registration-run");
        m_group.groupKey = GroupKey{QStringLiteral("reg|160x120|Lum")};
        m_group.settings.autoReferenceDuringRegistration = false;
        m_group.settings.enrichWithGaiaCatalog = false;
    }

    QString writeLight(const QString& name, const std::vector<FixtureStar>& stars, std::vector<FixtureCard> cards = {},
                       std::uint64_t seed = 1u)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        cards.push_back(exposureStartCard("2026-03-01T01:00:00"));
        std::string error;
        EXPECT_TRUE(writeStarField(path, stars, cards, seed, error)) << error;
        return path;
    }

    /// Writes the light as a colour camera's filter array samples it, which the registration reads in colour.
    QString writeMosaicLight(const QString& name, const std::vector<FixtureStar>& stars, std::uint64_t seed = 1u)
    {
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/") + name + QStringLiteral(".fits"));
        std::string error;
        EXPECT_TRUE(writeMosaicStarField(path, stars, {exposureStartCard("2026-03-01T01:00:00")}, seed, error))
            << error;
        return path;
    }

    /// How many warnings since the first `since` hold the text.
    [[nodiscard]] qsizetype warningsSaying(const QString& text, const qsizetype since = 0) const
    {
        const QStringList& warnings = m_run.loggedWarnings();
        return std::count_if(warnings.begin() + since, warnings.end(),
                             [&text](const QString& warning) { return warning.contains(text, Qt::CaseInsensitive); });
    }

    /// Writes a flat master of one sixteen-bit code over a star field's extent, zero at the samples stated, under the
    /// filter array of the lights it calibrates.
    QString writeFlatMaster(const QString& name,
                            const std::vector<an::tests::fixtures::FixtureSampleOverride>& zeros = {},
                            const an::tests::fixtures::FixtureCfa cfa = an::tests::fixtures::FixtureCfa::None)
    {
        an::tests::fixtures::AcquisitionFacts facts;
        facts.kind = "Flat Field";
        an::tests::fixtures::FixtureImage flat =
            an::tests::fixtures::acquisitionFrame(name.toStdString(), facts, an::tests::integration::kStarFieldWidth,
                                                  an::tests::integration::kStarFieldHeight);
        flat.levels = {an::tests::fixtures::FixtureChannelLevels{.base = 20000.0}};
        flat.overrides = zeros;
        flat.cfa = cfa;
        const QString path = QDir(m_root.path()).filePath(QStringLiteral("masters/") + name + QStringLiteral(".fits"));
        std::string error;
        EXPECT_TRUE(an::tests::fixtures::writeAcquisitionFrame(flat, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// Writes the flat master that is zero at the dead sample - outside the central third its level is read from, so
    /// it divides by one everywhere else - and leaves a light it calibrates not a number there.
    QString writeDeadFlat()
    {
        return writeFlatMaster(QStringLiteral("flat_dead"),
                               {an::tests::fixtures::FixtureSampleOverride{.x = kDeadX, .y = kDeadY, .value = 0.0}});
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

    /// Where the dead flat is zero: in a corner, away from every star a star field holds.
    static constexpr std::uint32_t kDeadX = 6u;
    static constexpr std::uint32_t kDeadY = 100u;

    /// Whether no star lies near enough to the dead sample for its light to reach it.
    [[nodiscard]] static bool clearOfTheDeadSample(const std::vector<FixtureStar>& stars)
    {
        return std::all_of(stars.begin(), stars.end(), [](const FixtureStar& star) {
            return std::hypot(star.x - static_cast<double>(kDeadX), star.y - static_cast<double>(kDeadY)) > 8.0;
        });
    }

    /// The reference's stars and two lights' stars moved from them, the lights written in that order.
    struct ShiftedLights
    {
        std::vector<FixtureStar> reference;
        std::vector<std::vector<FixtureStar>> moved;
        std::vector<QString> paths;
    };

    /// Writes a reference and two lights shifted from it, clear of the dead sample.
    ShiftedLights writeShiftedLights()
    {
        ShiftedLights lights;
        lights.reference = starCatalogue(36u, 7u);
        lights.moved = {movedStars(lights.reference, translation(2.6, -1.9)),
                        movedStars(lights.reference, translation(-1.4, 2.2))};
        EXPECT_TRUE(clearOfTheDeadSample(lights.reference)) << "a star lies on the sample the flat is zero at";
        lights.paths.push_back(writeLight(QStringLiteral("light_001"), lights.reference, {}, 11u));
        for (std::size_t index = 0; index < lights.moved.size(); ++index)
        {
            EXPECT_TRUE(clearOfTheDeadSample(lights.moved[index])) << "a star lies on the sample the flat is zero at";
            lights.paths.push_back(writeLight(QStringLiteral("light_%1").arg(index + 2u, 3, 10, QLatin1Char('0')),
                                              lights.moved[index], {}, 12u + index));
        }
        return lights;
    }

    /// The group of the shifted lights registered by an affine model.
    void useShiftedLights(const ShiftedLights& lights)
    {
        m_group.settings.transformMode = QStringLiteral("Linear");
        m_group.settings.transformation = QStringLiteral("Affine");
        useLights(lights.paths);
    }

    /// Every shifted light was registered, its stars landing on the reference's.
    void expectEveryShiftedLightRegistered(const ShiftedLights& lights)
    {
        const RecordedTransforms transforms = recordedTransforms();
        EXPECT_EQ(transforms.size(), lights.paths.size());
        for (std::size_t index = 0; index < lights.moved.size(); ++index)
        {
            const QString& path = lights.paths[index + 1u];
            const auto record = transforms.find(FrameId(path).canonicalPath());
            ASSERT_NE(record, transforms.end()) << "a light was left unregistered: " << path.toStdString();
            expectStarsLandOnTheReference(record->second.transform, lights.moved[index], lights.reference, 0.15);
        }
    }

    /// No light's detection failed, and none was searched again without its calibration.
    void expectEveryLightSearchedCalibrated() const
    {
        for (const QString& warning : m_run.loggedWarnings())
        {
            EXPECT_FALSE(warning.contains(QStringLiteral("Star detection failed"))) << warning.toStdString();
            EXPECT_FALSE(warning.contains(QStringLiteral("without calibration"))) << warning.toStdString();
        }
    }

    /// The group of these lights, the first its reference.
    void useLights(const std::vector<QString>& paths)
    {
        m_group.lightFiles.clear();
        for (const QString& path : paths)
        {
            m_group.lightFiles.emplace_back(path);
        }
        m_group.referenceFrame = m_group.lightFiles.front();
        m_group.settings.referenceImage = m_group.lightFiles.front().canonicalPath();
        m_manifest.registrationGroups = {m_group};
    }

    StageResult registerLights()
    {
        m_manifest.registrationGroups = {m_group};
        LightsRegistrationStage stage;
        return m_run.run(stage, m_manifest);
    }

    [[nodiscard]] QString metricsPath() const
    {
        return QDir(m_manifest.outputDirectory).filePath(QStringLiteral("registration_metrics.db"));
    }

    [[nodiscard]] RecordedTransforms recordedTransforms() const
    {
        RecordedTransforms transforms;
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("RegistrationTestReader"));
        QString error;
        EXPECT_TRUE(loader.open(&error)) << error.toStdString();
        std::vector<an::ui::RegistrationMetricRecord> records;
        EXPECT_TRUE(loader.loadMetricRecords(m_group.groupKey.toString(), m_group.settings.version, records, &error))
            << error.toStdString();
        for (const auto& record : records)
        {
            if (record.transformJson.isEmpty())
            {
                continue;
            }
            const auto parsed = stage_utils::parseTransformRecord(record.transformJson, error);
            EXPECT_TRUE(parsed.has_value()) << error.toStdString();
            if (parsed.has_value())
            {
                transforms.emplace(record.frameId.canonicalPath(), *parsed);
            }
        }
        return transforms;
    }

    [[nodiscard]] std::optional<an::ui::RegistrationGroupState> groupState() const
    {
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("RegistrationTestState"));
        QString error;
        EXPECT_TRUE(loader.open(&error)) << error.toStdString();
        return loader.loadGroupState(m_group.groupKey.toString(), m_group.settings.version, &error);
    }

    /// Every star of a light, mapped onto the reference, lands on the reference's star.
    static void expectStarsLandOnTheReference(const an::RegistrationTransform& transform,
                                              const std::vector<FixtureStar>& light,
                                              const std::vector<FixtureStar>& reference, double tolerance)
    {
        ASSERT_EQ(light.size(), reference.size());
        for (std::size_t index = 0; index < light.size(); ++index)
        {
            const an::Result<an::PixelCoordinate> mapped =
                an::mapRegistrationPoint(transform, enginePointOf(light[index]));
            ASSERT_TRUE(mapped.hasValue());
            const an::PixelCoordinate expected = enginePointOf(reference[index]);
            EXPECT_NEAR(mapped->x, expected.x, tolerance) << "star " << index;
            EXPECT_NEAR(mapped->y, expected.y, tolerance) << "star " << index;
        }
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    WorkflowManifest m_manifest;
    WorkflowManifest::RegistrationGroup m_group;
};

struct ModelCase
{
    const char* name;
    const char* transformMode;
    const char* transformation;
    int order;
    Motion (*motion)();
    bool linear;
};

class StarMatchingModels : public LightsRegistrationTest, public ::testing::WithParamInterface<ModelCase>
{
};

TEST_P(StarMatchingModels, EachLightIsRegisteredOntoTheReferenceInEnginePixelCoordinates)
{
    const ModelCase& model = GetParam();
    const std::vector<FixtureStar> reference = starCatalogue(36u, 7u);
    const std::vector<FixtureStar> moved = movedStars(reference, model.motion());
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 11u);
    const QString light_path = writeLight(QStringLiteral("light_002"), moved, {}, 12u);
    m_group.settings.transformMode = QString::fromLatin1(model.transformMode);
    m_group.settings.transformation = QString::fromLatin1(model.transformation);
    m_group.settings.transformOrder = model.order;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();

    const RecordedTransforms transforms = recordedTransforms();
    ASSERT_EQ(transforms.size(), 2u);
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    EXPECT_EQ(light->second.transform.isLinear(), model.linear);
    // A registration matched raw stars, so the transform holds every distortion between the frames: alignment must
    // not undo a header's distortion under it.
    EXPECT_EQ(light->second.provenance.policy, stage_utils::SourceDistortionPolicy::ExplicitNone);
    EXPECT_EQ(light->second.schemaVersion, 5);
    expectStarsLandOnTheReference(light->second.transform, moved, reference, 0.15);

    const auto own = transforms.find(FrameId(reference_path).canonicalPath());
    ASSERT_NE(own, transforms.end());
    const an::Result<an::PixelCoordinate> fixed = an::mapRegistrationPoint(own->second.transform, {40.5, 30.5});
    ASSERT_TRUE(fixed.hasValue());
    EXPECT_NEAR(fixed->x, 40.5, 1.0e-9);
    EXPECT_NEAR(fixed->y, 30.5, 1.0e-9);
}

INSTANTIATE_TEST_SUITE_P(Models, StarMatchingModels,
                         ::testing::Values(ModelCase{"Affine", "Linear", "Affine", 0, &affineMotion, true},
                                           ModelCase{"Projective", "Linear", "Homography", 0, &projectiveMotion, true},
                                           ModelCase{"Polynomial", "Polynomial", "Homography", 2, &bentMotion, false}),
                         [](const ::testing::TestParamInfo<ModelCase>& info) { return std::string(info.param.name); });

TEST_F(LightsRegistrationTest, AWideRunRecordsWhatOneLightAtATimeRecords)
{
    // Wide enough to read the next lights, measure two of them and search another at once - each on a thread of its
    // own in an operation of its own - the run records the registration a run of two threads, which does each light
    // in turn, records: the same transforms, stars and noise figures, to the bit.
    const std::vector<FixtureStar> reference = starCatalogue(36u, 7u);
    std::vector<QString> paths{writeLight(QStringLiteral("light_001"), reference, {}, 11u)};
    for (std::uint32_t index = 2u; index <= 7u; ++index)
    {
        const double dx = 0.37 * index;
        const double dy = -0.21 * index;
        paths.push_back(
            writeLight(QStringLiteral("light_%1").arg(index, 3, 10, QLatin1Char('0')),
                       movedStars(reference, [dx, dy](double x, double y) { return std::pair{x + dx, y + dy}; }), {},
                       11u + index));
    }
    useLights(paths);
    const auto recordsOf = [this](const QString& outputDirectory) {
        an::ui::RegistrationMetricsStateLoader loader(
            QDir(outputDirectory).filePath(QStringLiteral("registration_metrics.db")), QStringLiteral("WidthCompare"));
        QString error;
        EXPECT_TRUE(loader.open(&error)) << error.toStdString();
        std::vector<an::ui::RegistrationMetricRecord> records;
        EXPECT_TRUE(loader.loadMetricRecords(m_group.groupKey.toString(), m_group.settings.version, records, &error))
            << error.toStdString();
        std::map<QString, an::ui::RegistrationMetricRecord> byFrame;
        for (auto& record : records)
        {
            byFrame.emplace(record.frameId.canonicalPath(), std::move(record));
        }
        return byFrame;
    };

    const StageResult narrow = registerLights();
    ASSERT_TRUE(narrow.isSuccess()) << narrow.error.toLogString().toStdString();
    const auto narrowRecords = recordsOf(m_manifest.outputDirectory);

    CalibrationRun wide(16u);
    ASSERT_TRUE(wide.valid());
    WorkflowManifest wideManifest = m_manifest;
    wideManifest.outputDirectory = QDir(m_root.path()).filePath(QStringLiteral("wide-output"));
    LightsRegistrationStage stage;
    const StageResult wideResult = wide.run(stage, wideManifest);
    ASSERT_TRUE(wideResult.isSuccess()) << wideResult.error.toLogString().toStdString();
    const auto wideRecords = recordsOf(wideManifest.outputDirectory);

    ASSERT_EQ(narrowRecords.size(), paths.size());
    ASSERT_EQ(wideRecords.size(), narrowRecords.size());
    for (const auto& [frame, expected] : narrowRecords)
    {
        const auto found = wideRecords.find(frame);
        ASSERT_NE(found, wideRecords.end()) << frame.toStdString();
        const an::ui::RegistrationMetricRecord& actual = found->second;
        EXPECT_EQ(actual.transformJson, expected.transformJson) << frame.toStdString();
        EXPECT_EQ(actual.metrics.stars, expected.metrics.stars) << frame.toStdString();
        EXPECT_EQ(actual.metrics.fwhm, expected.metrics.fwhm) << frame.toStdString();
        EXPECT_EQ(actual.metrics.snr, expected.metrics.snr) << frame.toStdString();
        EXPECT_EQ(actual.metrics.eccentricity, expected.metrics.eccentricity) << frame.toStdString();
        EXPECT_EQ(actual.noise.background_noise, expected.noise.background_noise) << frame.toStdString();
        EXPECT_EQ(actual.noise.global_mad, expected.noise.global_mad) << frame.toStdString();
        EXPECT_EQ(actual.noise.global_scale, expected.noise.global_scale) << frame.toStdString();
        EXPECT_GT(actual.noise.global_scale, 0.0) << "the robust scale was not measured";
    }
}

TEST_F(LightsRegistrationTest, ASecondRunReusesTheRegistrationAndTheStarsItDetected)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 3u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 21u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(3.25, -2.5)), {}, 22u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    useLights({reference_path, light_path});

    ASSERT_TRUE(registerLights().isSuccess());
    const RecordedTransforms first = recordedTransforms();
    const auto state = groupState();
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->referenceFrameId, FrameId(reference_path).canonicalPath());

    an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("RegistrationTestCache"));
    ASSERT_TRUE(loader.open());
    std::unordered_map<QString, an::ui::RegistrationStarCacheEntry> cache;
    ASSERT_TRUE(loader.loadStarCacheForGroup(m_group.groupKey.toString(), state->detectionHash, cache));
    ASSERT_EQ(cache.size(), 2u);
    for (const auto& [key, entry] : cache)
    {
        (void)key;
        EXPECT_GE(entry.starsCount, 25) << entry.inputPath.toStdString();
    }

    ASSERT_TRUE(registerLights().isSuccess());
    const RecordedTransforms second = recordedTransforms();
    ASSERT_EQ(second.size(), first.size());
    for (const auto& [frame, record] : first)
    {
        const auto again = second.find(frame);
        ASSERT_NE(again, second.end());
        EXPECT_TRUE(again->second.transform == record.transform) << frame.toStdString();
    }
}

// The most stars a frame is detected with bounds the detection, so a group whose cap changed detects its lights
// again. It registered them again from the stars detected under the old cap, and a raised cap changed nothing.
TEST_F(LightsRegistrationTest, AChangedStarCapDetectsTheLightsAgain)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 3u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 21u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(3.25, -2.5)), {}, 22u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    m_group.settings.maxStarPairs = 20;
    useLights({reference_path, light_path});
    // The stars the group's current detections hold, light by light.
    const auto cachedStarCounts = [this]() {
        std::vector<int> counts;
        const auto state = groupState();
        EXPECT_TRUE(state.has_value());
        if (!state.has_value())
        {
            return counts;
        }
        an::ui::RegistrationMetricsStateLoader loader(metricsPath(), QStringLiteral("RegistrationTestCache"));
        EXPECT_TRUE(loader.open());
        std::unordered_map<QString, an::ui::RegistrationStarCacheEntry> cache;
        EXPECT_TRUE(loader.loadStarCacheForGroup(m_group.groupKey.toString(), state->detectionHash, cache));
        for (const auto& [key, entry] : cache)
        {
            (void)key;
            counts.push_back(entry.starsCount);
        }
        return counts;
    };

    ASSERT_TRUE(registerLights().isSuccess());
    const std::vector<int> capped = cachedStarCounts();
    ASSERT_EQ(capped.size(), 2u);
    for (const int count : capped)
    {
        EXPECT_LE(count, 20);
    }

    m_group.settings.maxStarPairs = 2000;
    ASSERT_TRUE(registerLights().isSuccess());
    const std::vector<int> raised = cachedStarCounts();
    ASSERT_EQ(raised.size(), 2u);
    for (const int count : raised)
    {
        EXPECT_GE(count, 25);
    }
}

TEST_F(LightsRegistrationTest, PhaseCorrelationRecoversATranslation)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 5u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 31u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(5.0, -3.0)), {}, 32u);
    m_group.settings.registrationMethod = QStringLiteral("phase_correlation");
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const RecordedTransforms transforms = recordedTransforms();
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(light->second.transform, {60.5, 50.5});
    ASSERT_TRUE(mapped.hasValue());
    EXPECT_NEAR(mapped->x, 55.5, 0.15);
    EXPECT_NEAR(mapped->y, 53.5, 0.15);
}

// A group with no reference chosen is registered onto its first light, which the run log never named.
TEST_F(LightsRegistrationTest, AGroupWithNoReferenceChosenNamesTheFirstLightItIsRegisteredOnto)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 5u);
    const QString first_path = writeLight(QStringLiteral("light_001"), reference, {}, 31u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(5.0, -3.0)), {}, 32u);
    m_group.settings.registrationMethod = QStringLiteral("phase_correlation");
    useLights({first_path, light_path});
    m_group.referenceFrame.reset();
    m_group.settings.referenceImage.clear();

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const QString named = QStringLiteral("Reference frame: %1 (first in group)").arg(FrameId(first_path).toString());
    const QStringList& details = m_run.loggedDetails();
    EXPECT_TRUE(
        std::any_of(details.begin(), details.end(), [&named](const QString& detail) { return detail.contains(named); }))
        << "no detail says " << named.toStdString();
    const auto state = groupState();
    ASSERT_TRUE(state.has_value());
    ASSERT_EQ(recordedTransforms().size(), 2u);
}

// Phase correlation over colour lights corrected for chromatic aberration and calibrated in memory measures each shift
// on the corrected light the alignment reads, which the correction wrote from the light as calibrated.
TEST_F(LightsRegistrationTest, PhaseCorrelationOverCorrectedLightsCalibratedInMemoryMeasuresTheirShift)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 5u);
    const QString reference_path = writeMosaicLight(QStringLiteral("light_001"), reference, 31u);
    const QString light_path =
        writeMosaicLight(QStringLiteral("light_002"), movedStars(reference, translation(5.0, -3.0)), 32u);
    const QString flat = writeFlatMaster(QStringLiteral("flat"), {}, an::tests::fixtures::FixtureCfa::Rggb);
    calibrateInMemoryBy(reference_path, flat);
    calibrateInMemoryBy(light_path, flat);
    m_group.settings.registrationMethod = QStringLiteral("phase_correlation");
    m_group.settings.chromaticAberration.enabled = true;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    for (const QString& path : {reference_path, light_path})
    {
        EXPECT_TRUE(QFileInfo::exists(chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(path))))
            << path.toStdString() << "\n"
            << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
    }
    const RecordedTransforms transforms = recordedTransforms();
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(light->second.transform, {60.5, 50.5});
    ASSERT_TRUE(mapped.hasValue());
    EXPECT_NEAR(mapped->x, 55.5, 0.15);
    EXPECT_NEAR(mapped->y, 53.5, 0.15);
}

// A reference holding fewer stars than the minimum lowers the minimum for the run, but no star match is accepted on
// fewer than five pairs: the warning promised a minimum of the reference's four stars, which no match was held to. It
// says what the run holds, and the light the reference's four stars cannot match is left unregistered with the reason.
TEST_F(LightsRegistrationTest, AReferenceOfFewerStarsThanAMatchNeedsIsSaidAsTheMatchHoldsIt)
{
    const std::vector<FixtureStar> sky = starCatalogue(36u, 7u);
    const std::vector<FixtureStar> four(sky.begin(), sky.begin() + 4);
    const QString reference_path = writeLight(QStringLiteral("light_001"), four, {}, 11u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(sky, translation(2.6, -1.9)), {}, 12u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    m_group.settings.minStarPairs = 10;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    EXPECT_FALSE(result.isSuccess() && !result.isPartialSuccess()) << "the light was registered by four stars";
    EXPECT_EQ(recordedTransforms().count(FrameId(light_path).canonicalPath()), 0u);
    const QStringList& warnings = m_run.loggedWarnings();
    EXPECT_FALSE(std::any_of(warnings.begin(), warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("to 4 "));
    })) << "a warning promised a minimum of four pairs";
    EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("4 stars")) && warning.contains(QStringLiteral("at least 5"));
    })) << "no warning says the reference's four stars are fewer than a match needs";
}

// A reference holding fewer stars than the minimum, but as many as a match needs, lowers the minimum to its stars, and
// its lights are matched by them.
TEST_F(LightsRegistrationTest, AReferenceOfFewerStarsThanTheMinimumLowersItToItsStars)
{
    const std::vector<FixtureStar> sky = starCatalogue(36u, 7u);
    const std::vector<FixtureStar> eight(sky.begin(), sky.begin() + 8);
    const QString reference_path = writeLight(QStringLiteral("light_001"), eight, {}, 11u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(eight, translation(2.6, -1.9)), {}, 12u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    m_group.settings.minStarPairs = 10;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "the light was left unregistered";
    const QStringList& warnings = m_run.loggedWarnings();
    EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("8 stars")) && warning.contains(QStringLiteral("to 8 "));
    })) << "no warning says the minimum was lowered to the reference's eight stars";
    const RecordedTransforms transforms = recordedTransforms();
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    expectStarsLandOnTheReference(light->second.transform, movedStars(eight, translation(2.6, -1.9)), eight, 0.15);
}

// A polynomial of a stated order needs as many star pairs as it has terms - fifteen for the fourth order - and a light
// matched by fewer failed though its star match had succeeded. It keeps the linear transform its match found, and the
// run says so for the light.
TEST_F(LightsRegistrationTest, ALightTooFewPairsCarryTheStatedOrderForIsRegisteredLinearlyAndSaid)
{
    const std::vector<FixtureStar> sky = starCatalogue(36u, 7u);
    const std::vector<FixtureStar> six(sky.begin(), sky.begin() + 6);
    const QString reference_path = writeLight(QStringLiteral("light_001"), six, {}, 11u);
    const std::vector<FixtureStar> moved = movedStars(six, translation(2.6, -1.9));
    const QString light_path = writeLight(QStringLiteral("light_002"), moved, {}, 12u);
    m_group.settings.transformMode = QStringLiteral("Polynomial");
    m_group.settings.transformation = QStringLiteral("Homography");
    m_group.settings.transformOrder = 4;
    m_group.settings.minStarPairs = 6;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "the light was left unregistered";
    const RecordedTransforms transforms = recordedTransforms();
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    EXPECT_TRUE(light->second.transform.isLinear());
    expectStarsLandOnTheReference(light->second.transform, moved, six, 0.15);
    const QString name = QFileInfo(light_path).fileName();
    const QStringList& warnings = m_run.loggedWarnings();
    EXPECT_EQ(std::count_if(warnings.begin(), warnings.end(),
                            [&name](const QString& warning) {
                                return warning.contains(name) && warning.contains(QStringLiteral("6 star pairs")) &&
                                       warning.contains(QStringLiteral("15")) &&
                                       warning.contains(QStringLiteral("order 4")) &&
                                       warning.contains(QStringLiteral("linear"));
                            }),
              1)
        << "the light's linear transform was not said once";
}

// The detector refuses a roundness threshold that is not positive, and the dialog's slider started at zero: every
// light's detection failed there. The run detects at the smallest threshold the detector takes, registers every light
// and says so once for the group.
TEST_F(LightsRegistrationTest, ARoundnessThresholdOfZeroRunsAtTheDetectorsSmallestAndIsSaidOnce)
{
    const ShiftedLights lights = writeShiftedLights();
    useShiftedLights(lights);
    m_group.settings.roundnessThreshold = 0.0;

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    const QStringList& warnings = m_run.loggedWarnings();
    for (const QString& warning : warnings)
    {
        EXPECT_FALSE(warning.contains(QStringLiteral("Star detection failed"))) << warning.toStdString();
    }
    EXPECT_EQ(std::count_if(warnings.begin(), warnings.end(),
                            [](const QString& warning) {
                                return warning.contains(QStringLiteral("roundness threshold 0.00 as 0.01"));
                            }),
              1);
    expectEveryShiftedLightRegistered(lights);
}

TEST_F(LightsRegistrationTest, ControlPointsAreFittedAsThePersonPlacedThem)
{
    const QString reference_path = writeLight(QStringLiteral("light_001"), {}, {}, 41u);
    const QString light_path = writeLight(QStringLiteral("light_002"), {}, {}, 42u);
    m_group.settings.registrationMethod = QStringLiteral("manual_points");
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    const auto affine = [](QPointF point) {
        return QPointF(1.01 * point.x() - 0.02 * point.y() + 4.0, 0.015 * point.x() + 0.99 * point.y() - 3.0);
    };
    RegistrationSettings::ManualControlPointList points;
    for (const QPointF source :
         {QPointF(20.5, 15.5), QPointF(140.5, 18.5), QPointF(30.5, 100.5), QPointF(130.5, 105.5), QPointF(80.5, 60.5)})
    {
        points.push_back(RegistrationSettings::ManualControlPoint{source, affine(source)});
    }
    useLights({reference_path, light_path});
    m_group.settings.manualControlPointsByFrame.insert(FrameId(light_path).canonicalPath(), points);

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const RecordedTransforms transforms = recordedTransforms();
    const auto light = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(light, transforms.end());
    for (const QPointF probe : {QPointF(50.0, 40.0), QPointF(120.0, 90.0)})
    {
        const an::Result<an::PixelCoordinate> mapped =
            an::mapRegistrationPoint(light->second.transform, {probe.x(), probe.y()});
        ASSERT_TRUE(mapped.hasValue());
        EXPECT_NEAR(mapped->x, affine(probe).x(), 1.0e-6);
        EXPECT_NEAR(mapped->y, affine(probe).y(), 1.0e-6);
    }
}

TEST_F(LightsRegistrationTest, StarlessLightsAreRegisteredThroughTheirSolutionsDistortionIncluded)
{
    const QString reference_path = writeLight(QStringLiteral("light_001"), {}, tanSolutionCards(80.5, 60.5), 51u);
    const QString shifted_path = writeLight(QStringLiteral("light_002"), {}, tanSolutionCards(86.5, 56.5), 52u);
    const QString distorted_path =
        writeLight(QStringLiteral("light_003"), {}, tanSolutionCards(78.5, 63.5, 2.0e-5), 53u);
    useLights({reference_path, shifted_path, distorted_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    const RecordedTransforms transforms = recordedTransforms();
    ASSERT_EQ(transforms.size(), 3u);

    an::Result<an::consumer::OperationScope> scope = m_run.session().admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    ASSERT_TRUE(scope.hasValue());
    const auto reference_facts = stage_utils::probeRegistrationFrame(*scope, nullptr, reference_path);
    ASSERT_TRUE(reference_facts.hasValue());
    ASSERT_TRUE(reference_facts->solution.has_value());
    for (const QString& path : {shifted_path, distorted_path})
    {
        const auto facts = stage_utils::probeRegistrationFrame(*scope, nullptr, path);
        ASSERT_TRUE(facts.hasValue());
        ASSERT_TRUE(facts->solution.has_value());
        const auto record = transforms.find(FrameId(path).canonicalPath());
        ASSERT_NE(record, transforms.end());
        for (const an::PixelCoordinate probe :
             std::array<an::PixelCoordinate, 4>{{{30.5, 25.5}, {130.5, 25.5}, {30.5, 95.5}, {120.5, 90.5}}})
        {
            const auto sky = facts->solution->pixelToWorld(probe);
            ASSERT_TRUE(sky.hasValue());
            const auto expected = reference_facts->solution->worldToPixel(*sky);
            ASSERT_TRUE(expected.hasValue());
            const auto mapped = an::mapRegistrationPoint(record->second.transform, probe);
            ASSERT_TRUE(mapped.hasValue());
            EXPECT_NEAR(mapped->x, expected->x, 0.05) << path.toStdString();
            EXPECT_NEAR(mapped->y, expected->y, 0.05) << path.toStdString();
        }
    }

    // The plane the lights are aligned onto is the reference's, and its record states so.
    const auto state = groupState();
    ASSERT_TRUE(state.has_value());
    ASSERT_FALSE(state->outputWcsJson.isEmpty());
    std::vector<an::CodecCard> cards;
    stage_utils::OutputPlaneWcsDescriptorInfo info;
    QString error;
    ASSERT_TRUE(stage_utils::parseOutputPlaneWcsJson(state->outputWcsJson, &cards, &info, error))
        << error.toStdString();
    EXPECT_EQ(info.sourceKind, QStringLiteral("reference_frame"));
    EXPECT_EQ(info.width, static_cast<int>(an::tests::integration::kStarFieldWidth));
    EXPECT_EQ(info.height, static_cast<int>(an::tests::integration::kStarFieldHeight));
    EXPECT_FALSE(cards.empty());
}

// The reference shares a few stars with the far light, fewer than a match needs; the near light shares two dozen with
// each. The far light is matched to the near one, whose matched stars the near light's transform carries onto the
// reference, and its transform is fitted to the pairs that makes. A light no registered light matches is left out.
TEST_F(LightsRegistrationTest, ALightTheReferenceHoldsTooFewStarsOfIsRegisteredThroughALightRegisteredNearIt)
{
    const std::vector<FixtureStar> sky = wideSky();
    const WindowStars reference_window = windowStars(sky, {});
    const WindowStars near_window = windowStars(sky, {.offsetX = 65.0, .offsetY = 1.5, .degrees = 0.5});
    const WindowStars far_window = windowStars(sky, {.offsetX = 125.0, .offsetY = -1.0, .degrees = -0.4});
    ASSERT_GE(sharedStars(reference_window, near_window), 20u);
    ASSERT_GE(sharedStars(near_window, far_window), 20u);
    ASSERT_LT(sharedStars(reference_window, far_window), 5u)
        << "the reference holds enough of the far light's stars to match it";
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference_window.stars, {}, 71u);
    const QString near_path = writeLight(QStringLiteral("light_002"), near_window.stars, {}, 72u);
    const QString far_path = writeLight(QStringLiteral("light_003"), far_window.stars, {}, 73u);
    const QString unrelated_path = writeLight(QStringLiteral("light_004"), starCatalogue(14u, 77u), {}, 74u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    useLights({reference_path, near_path, far_path, unrelated_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.isPartialSuccess()) << "the unrelated light was registered";

    const RecordedTransforms transforms = recordedTransforms();
    const auto near_record = transforms.find(FrameId(near_path).canonicalPath());
    ASSERT_NE(near_record, transforms.end());
    expectStarsLandOnTheReference(near_record->second.transform, near_window.stars, onTheReference(sky, near_window),
                                  0.15);

    const auto far_record = transforms.find(FrameId(far_path).canonicalPath());
    ASSERT_NE(far_record, transforms.end()) << "the far light was not registered through the near one";
    EXPECT_EQ(far_record->second.provenance.policy, stage_utils::SourceDistortionPolicy::ExplicitNone);
    EXPECT_EQ(far_record->second.schemaVersion, 5);
    expectStarsLandOnTheReference(far_record->second.transform, far_window.stars, onTheReference(sky, far_window),
                                  0.15);

    EXPECT_EQ(transforms.count(FrameId(unrelated_path).canonicalPath()), 0u)
        << "a light whose stars no registered light holds was registered";
}

// A light looking onto the sky well over half a light from the reference shares about a third of its stars with it -
// over the ten pairs a match needs - and has no other light to be matched through. Nearly every star the reference
// holds pairs with one of the light's, so its pairs are under half of the candidates; a match of ten candidate pairs or
// more is accepted on its pairs - this light matches by sixteen pairs of its forty candidates - so the
// light is registered by the stars it shares.
TEST_F(LightsRegistrationTest, ALightSharingAThirdOfItsStarsWithTheReferenceIsRegisteredByTheStarsItShares)
{
    const std::vector<FixtureStar> sky = wideSky();
    const WindowStars reference_window = windowStars(sky, {});
    const WindowStars light_window = windowStars(sky, {.offsetX = 90.0, .offsetY = 1.5, .degrees = 0.5});
    const std::size_t shared = sharedStars(reference_window, light_window);
    ASSERT_GE(shared, 14u) << "the light shares too few stars with the reference";
    ASSERT_LT(shared * 5u, light_window.stars.size() * 2u)
        << "the light shares two fifths of its stars or more with the reference";
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference_window.stars, {}, 91u);
    const QString light_path = writeLight(QStringLiteral("light_002"), light_window.stars, {}, 92u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    const RecordedTransforms transforms = recordedTransforms();
    const auto record = transforms.find(FrameId(light_path).canonicalPath());
    ASSERT_NE(record, transforms.end()) << "the light was refused though it shares more stars than a match needs";
    expectStarsLandOnTheReference(record->second.transform, light_window.stars, onTheReference(sky, light_window),
                                  0.15);
}

// A light whose stars neither the reference nor a light registered near it holds is still registered from its
// solution when it and the reference are solved.
TEST_F(LightsRegistrationTest, ALightNoRegisteredLightMatchesIsStillRegisteredFromItsSolution)
{
    const std::vector<FixtureStar> sky = wideSky();
    const WindowStars reference_window = windowStars(sky, {});
    const WindowStars near_window = windowStars(sky, {.offsetX = 65.0, .offsetY = 1.5, .degrees = 0.5});
    const QString reference_path =
        writeLight(QStringLiteral("light_001"), reference_window.stars, tanSolutionCards(80.5, 60.5), 81u);
    const QString near_path = writeLight(QStringLiteral("light_002"), near_window.stars, {}, 82u);
    const QString solved_path =
        writeLight(QStringLiteral("light_003"), starCatalogue(14u, 77u), tanSolutionCards(86.5, 56.5), 83u);
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    useLights({reference_path, near_path, solved_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    const RecordedTransforms transforms = recordedTransforms();
    ASSERT_EQ(transforms.size(), 3u);
    const auto near_record = transforms.find(FrameId(near_path).canonicalPath());
    ASSERT_NE(near_record, transforms.end());
    expectStarsLandOnTheReference(near_record->second.transform, near_window.stars, onTheReference(sky, near_window),
                                  0.15);

    an::Result<an::consumer::OperationScope> scope = m_run.session().admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    ASSERT_TRUE(scope.hasValue());
    const auto reference_facts = stage_utils::probeRegistrationFrame(*scope, nullptr, reference_path);
    const auto solved_facts = stage_utils::probeRegistrationFrame(*scope, nullptr, solved_path);
    ASSERT_TRUE(reference_facts.hasValue() && solved_facts.hasValue());
    ASSERT_TRUE(reference_facts->solution.has_value() && solved_facts->solution.has_value());
    const auto solved_record = transforms.find(FrameId(solved_path).canonicalPath());
    ASSERT_NE(solved_record, transforms.end());
    for (const an::PixelCoordinate probe :
         std::array<an::PixelCoordinate, 4>{{{30.5, 25.5}, {130.5, 25.5}, {30.5, 95.5}, {120.5, 90.5}}})
    {
        const auto on_sky = solved_facts->solution->pixelToWorld(probe);
        ASSERT_TRUE(on_sky.hasValue());
        const auto expected = reference_facts->solution->worldToPixel(*on_sky);
        ASSERT_TRUE(expected.hasValue());
        const auto mapped = an::mapRegistrationPoint(solved_record->second.transform, probe);
        ASSERT_TRUE(mapped.hasValue());
        EXPECT_NEAR(mapped->x, expected->x, 0.05);
        EXPECT_NEAR(mapped->y, expected->y, 0.05);
    }
}

// A light calibrated in memory is not a number where its flat is zero, and its stars are searched without that sample,
// as a light a flat without a zero calibrated is searched: every light is registered onto the reference. The lights the
// flat holding a zero calibrated were left unregistered, because the detection read each light as all valid and the
// engine refused the sample that is not a number when it measured the sky.
TEST_F(LightsRegistrationTest, ALightCalibratedInMemoryIsRegisteredWhereItsFlatIsZero)
{
    const ShiftedLights lights = writeShiftedLights();
    const QString flat = writeFlatMaster(QStringLiteral("flat"));
    const QString dead = writeDeadFlat();
    calibrateInMemoryBy(lights.paths[0], flat);
    calibrateInMemoryBy(lights.paths[1], dead);
    calibrateInMemoryBy(lights.paths[2], dead);
    useShiftedLights(lights);

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    expectEveryLightSearchedCalibrated();
    expectEveryShiftedLightRegistered(lights);
}

// Lights calibrated in memory by one flat holding a zero are each not a number there, and their stars are searched on
// the lights as calibrated, the reference's among them. The detection refused every light, so the reference held no
// star and the stage searched every light again as it was written, its calibration set aside.
TEST_F(LightsRegistrationTest, LightsCalibratedInMemoryByAFlatHoldingAZeroAreSearchedAsCalibrated)
{
    const ShiftedLights lights = writeShiftedLights();
    const QString dead = writeDeadFlat();
    for (const QString& path : lights.paths)
    {
        calibrateInMemoryBy(path, dead);
    }
    useShiftedLights(lights);

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    expectEveryLightSearchedCalibrated();
    expectEveryShiftedLightRegistered(lights);
}

TEST_F(LightsRegistrationTest, ChromaticCorrectionOutputsArePublishedStampedAndReused)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 19u);
    const QString reference_path = writeMosaicLight(QStringLiteral("light_001"), reference, 91u);
    const QString light_path =
        writeMosaicLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, -1.0)), 92u);
    m_group.settings.chromaticAberration.enabled = true;
    m_group.settings.transformMode = QStringLiteral("Linear");
    m_group.settings.transformation = QStringLiteral("Affine");
    useLights({reference_path, light_path});

    const StageResult first = registerLights();
    ASSERT_TRUE(first.isSuccess()) << first.error.toLogString().toStdString();
    std::vector<QDateTime> written;
    for (const QString& light : {reference_path, light_path})
    {
        // A colour light is published as the registration's input whether or not a channel of it moved.
        const QString output = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light));
        ASSERT_TRUE(QFileInfo::exists(output)) << output.toStdString();
        const auto stamp = an::tests::integration::writtenIdentity(m_run.session(), output);
        ASSERT_TRUE(stamp.has_value()) << output.toStdString();
        EXPECT_EQ(stamp->runId, "registration-run");
        EXPECT_FALSE(stamp->artifactId.empty());
        written.push_back(QFileInfo(output).lastModified());
    }
    EXPECT_EQ(recordedTransforms().size(), 2u);

    ASSERT_TRUE(registerLights().isSuccess());
    std::size_t index = 0;
    for (const QString& light : {reference_path, light_path})
    {
        const QString output = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light));
        EXPECT_EQ(QFileInfo(output).lastModified(), written[index++]) << output.toStdString();
    }
}

// The correction moves a colour light's red and blue onto its green, and a grey light has neither: over a grey group it
// moved nothing, wrote a copy of every light for the registration to read, and said nothing. The group's lights are
// registered as they are, no copy of one is written, and the group says once why nothing was corrected.
TEST_F(LightsRegistrationTest, ChromaticCorrectionOverLightsOfOneChannelWritesNoCopyAndIsSaidOnce)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 19u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 91u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, -1.0)), {}, 92u);
    m_group.settings.chromaticAberration.enabled = true;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    for (const QString& light : {reference_path, light_path})
    {
        const QString copy = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light));
        EXPECT_FALSE(QFileInfo::exists(copy)) << copy.toStdString();
    }
    EXPECT_EQ(warningsSaying(QStringLiteral("one channel")), 1)
        << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
    EXPECT_EQ(recordedTransforms().size(), 2u);
}

// Every later reader takes a chromatic aberration copy an earlier run left over its light, and a run's execution plan
// names the copies it found, so a grey group that has one keeps every copy current rather than leaving a stale one to
// be read; it still says why nothing was corrected.
TEST_F(LightsRegistrationTest, CopiesAnEarlierRunLeftOfLightsOfOneChannelAreKeptCurrent)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 19u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 91u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, -1.0)), {}, 92u);
    m_group.settings.chromaticAberration.enabled = true;
    useLights({reference_path, light_path});
    const QString earlier = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light_path));
    ASSERT_TRUE(QDir().mkpath(QFileInfo(earlier).absolutePath()));
    ASSERT_TRUE(QFile::copy(light_path, earlier));

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    for (const QString& light : {reference_path, light_path})
    {
        const QString copy = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light));
        ASSERT_TRUE(QFileInfo::exists(copy)) << copy.toStdString();
        const auto stamp = an::tests::integration::writtenIdentity(m_run.session(), copy);
        ASSERT_TRUE(stamp.has_value()) << copy.toStdString();
        EXPECT_EQ(stamp->runId, "registration-run") << copy.toStdString();
    }
    EXPECT_EQ(warningsSaying(QStringLiteral("one channel")), 1)
        << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
}

// A colour group in none of whose lights a channel moved - too few edge stars matched in each - said so only in the
// file log, channel by channel. Each light is still the registration's input as it was read, and the group says once
// that nothing was corrected.
TEST_F(LightsRegistrationTest, AChromaticCorrectionThatMovesNoChannelOfAnyLightIsSaidOnce)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 19u);
    const QString reference_path = writeMosaicLight(QStringLiteral("light_001"), reference, 91u);
    const QString light_path =
        writeMosaicLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, -1.0)), 92u);
    m_group.settings.chromaticAberration.enabled = true;
    // No match of a channel's edge stars holds fifty pairs among thirty stars.
    m_group.settings.chromaticAberration.minMatchedStars = 50;
    useLights({reference_path, light_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    for (const QString& light : {reference_path, light_path})
    {
        const QString copy = chromaticAberrationLightOutputPathFor(m_manifest.outputDirectory, FrameId(light));
        EXPECT_TRUE(QFileInfo::exists(copy)) << copy.toStdString();
    }
    EXPECT_EQ(warningsSaying(QStringLiteral("moved no channel")), 1)
        << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
    EXPECT_EQ(warningsSaying(QStringLiteral("one channel")), 0);
}

// Gaia enrichment left on where no catalog is installed was said only in the file log, so the run never named a setting
// it could not honour. A group with lights the catalog would have enriched says it once, however many they are; a group
// whose lights state no solution, which enrichment passes by in any case, says nothing.
TEST_F(LightsRegistrationTest, GaiaEnrichmentWithNoCatalogInstalledIsSaidOnceByAGroupWithSolvedLights)
{
    m_manifest.plateSolve.gaiaCatalogDirectory = QDir(m_root.path()).filePath(QStringLiteral("no_catalog"));
    m_group.settings.enrichWithGaiaCatalog = true;
    const std::vector<FixtureStar> sky = starCatalogue(30u, 29u);
    const std::array<std::pair<double, double>, 3u> shifts = {{{0.0, 0.0}, {1.5, -2.0}, {-2.5, 1.0}}};
    std::vector<QString> solved;
    for (std::size_t index = 0; index < shifts.size(); ++index)
    {
        const auto [dx, dy] = shifts[index];
        solved.push_back(writeLight(QStringLiteral("solved_%1").arg(index + 1u), movedStars(sky, translation(dx, dy)),
                                    tanSolutionCards(80.5 + dx, 60.5 + dy), 51u + index));
    }
    useLights(solved);
    WorkflowManifest::RegistrationGroup unsolved = m_group;
    unsolved.groupKey = GroupKey{QStringLiteral("reg|160x120|Unsolved")};
    unsolved.lightFiles = {
        FrameId(writeLight(QStringLiteral("unsolved_1"), sky, {}, 61u)),
        FrameId(writeLight(QStringLiteral("unsolved_2"), movedStars(sky, translation(2.0, 2.0)), {}, 62u))};
    unsolved.referenceFrame = unsolved.lightFiles.front();
    unsolved.settings.referenceImage = unsolved.lightFiles.front().canonicalPath();
    m_manifest.registrationGroups = {m_group, unsolved};

    LightsRegistrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest);
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_FALSE(result.isPartialSuccess()) << "a light was left unregistered";
    const QStringList& warnings = m_run.loggedWarnings();
    ASSERT_EQ(warningsSaying(QStringLiteral("Gaia")), 1) << warnings.join(QStringLiteral("\n")).toStdString();
    const auto said = std::find_if(warnings.begin(), warnings.end(), [](const QString& warning) {
        return warning.contains(QStringLiteral("Gaia"), Qt::CaseInsensitive);
    });
    EXPECT_TRUE(said->contains(QStringLiteral("3 solved light"))) << said->toStdString();
    EXPECT_TRUE(said->contains(QStringLiteral("no_catalog"))) << said->toStdString();
}

// The integration's registration may send a light to the plate solver when its group asks it to, and always on the
// mosaic canvas, which the dialog's Auto-solve never showed: with it off, a panel star matching could not register was
// sent to a configured online solver unasked. Where no solver is configured the dialog showed it off while the run
// said it was on. For each canvas, choice and solver, the solve the run allows is the one the dialog's control shows -
// on and fixed under Mosaic, off without a solver.
TEST_F(LightsRegistrationTest, ThePlateSolveARegistrationAllowsIsTheOneItsDialogShows)
{
    // The internal solver over a ready catalog that holds no star: a solver that reaches no network.
    QTemporaryDir catalog;
    ASSERT_TRUE(catalog.isValid());
    an::tests::integration::ScopedProcessWindowContext installed;
    ASSERT_TRUE(an::tests::integration::configureEmptyGaiaSolver(installed.context(), catalog.path()));
    const QString no_catalog = QDir(m_root.path()).filePath(QStringLiteral("no_catalog"));
    const std::vector<FixtureStar> reference = starCatalogue(30u, 23u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 41u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, 1.0)), {}, 42u);
    useLights({reference_path, light_path});

    struct Choice
    {
        QString canvas;
        bool autoSolve = false;
        bool solver = true;
    };
    for (const Choice& choice :
         {Choice{QStringLiteral("mosaic"), false, true}, Choice{QStringLiteral("standard"), false, true},
          Choice{QStringLiteral("standard"), true, true}, Choice{QStringLiteral("mosaic"), true, false}})
    {
        SCOPED_TRACE(choice.canvas.toStdString() + (choice.autoSolve ? ", Auto-solve on" : ", Auto-solve off") +
                     (choice.solver ? "" : ", no solver"));
        // The dialog reads the application's settings and the run the manifest's copy of them.
        const QString directory = choice.solver ? catalog.path() : no_catalog;
        ASSERT_TRUE(
            installed.context().upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY), directory));
        m_manifest.plateSolve.gaiaCatalogDirectory = directory;
        RegistrationSettingsDialog dialog(QStringLiteral("Group"), {reference_path, light_path});
        RegistrationSettings stated = m_group.settings;
        stated.alignmentMode = choice.canvas;
        stated.autoSolveUnsolvedFrames = choice.autoSolve;
        dialog.setRegistrationSettings(stated);
        const auto* auto_solve = dialog.findChild<QCheckBox*>(QStringLiteral("chkAutoSolveUnsolvedFrames"));
        ASSERT_NE(auto_solve, nullptr);
        m_group.settings = dialog.getRegistrationSettings();

        const qsizetype since = m_run.loggedDetails().size();
        const StageResult result = registerLights();
        ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
        const QStringList& details = m_run.loggedDetails();
        const auto plate_solve = std::find_if(details.begin() + since, details.end(), [](const QString& detail) {
            return detail.contains(QStringLiteral("Plate solve"));
        });
        ASSERT_TRUE(plate_solve != details.end()) << "the run states no plate solve";
        const QString shown =
            auto_solve->isChecked() ? QStringLiteral("effective=on") : QStringLiteral("effective=off");
        EXPECT_TRUE(plate_solve->contains(shown))
            << plate_solve->toStdString() << " where the dialog shows " << shown.toStdString();
    }
}

// On the mosaic canvas a light star matching cannot register is placed by the sky, through the plate solver - which
// was asked where none was configured, the log saying the solve was on. With no solver configured nothing is sent: the
// log says the solve is off, the light is left unregistered, and the run names the missing solver.
TEST_F(LightsRegistrationTest, AMosaicPanelNothingCanSolveIsLeftUnregisteredNamingTheMissingSolver)
{
    m_manifest.plateSolve.gaiaCatalogDirectory = QDir(m_root.path()).filePath(QStringLiteral("no_catalog"));
    const std::vector<FixtureStar> reference = starCatalogue(30u, 37u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 71u);
    const QString shifted_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(1.5, 2.0)), {}, 72u);
    const QString other_field_path = writeLight(QStringLiteral("light_003"), starCatalogue(30u, 38u), {}, 73u);
    m_group.settings.alignmentMode = QStringLiteral("mosaic");
    m_group.settings.autoSolveUnsolvedFrames = false;
    useLights({reference_path, shifted_path, other_field_path});

    const StageResult result = registerLights();
    ASSERT_TRUE(result.isSuccess()) << result.error.toLogString().toStdString();
    EXPECT_TRUE(result.isPartialSuccess()) << "the light of another field was registered";
    const QStringList& details = m_run.loggedDetails();
    const auto plate_solve = std::find_if(details.begin(), details.end(), [](const QString& detail) {
        return detail.contains(QStringLiteral("Plate solve"));
    });
    ASSERT_TRUE(plate_solve != details.end()) << "the run states no plate solve";
    EXPECT_TRUE(plate_solve->contains(QStringLiteral("effective=off"))) << plate_solve->toStdString();
    EXPECT_GE(warningsSaying(QStringLiteral("no plate solver is configured")), 1)
        << m_run.loggedWarnings().join(QStringLiteral("\n")).toStdString();
    const RecordedTransforms transforms = recordedTransforms();
    EXPECT_EQ(transforms.count(FrameId(other_field_path).canonicalPath()), 0u);
    EXPECT_EQ(transforms.count(FrameId(shifted_path).canonicalPath()), 1u);
}

// The integration demosaiced a light by its Bayer method over an X-Trans table, which the engine refuses, so no X-Trans
// light was registered, aligned, normalized or stacked; and a named two-by-two pattern over one was placed at its
// origin. A light is read in colour by the method's tabulated counterpart, and a named pattern is refused by name.
TEST_F(LightsRegistrationTest, AnXTransLightIsReadInColourByTheTabulatedCounterpartOfItsMethod)
{
    const QString path = QDir(m_root.path()).filePath(QStringLiteral("lights/xtrans.tif"));
    ASSERT_TRUE(an::tests::viewer::writeXTransTiff(m_run.session(), path));
    const auto read = [this, &path](an::DemosaicMethod method, const QString& pattern) -> an::Result<an::Image<float>> {
        an::Result<an::consumer::OperationScope> scope =
            m_run.session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        return stage_utils::readRegistrationImage(
            *scope, nullptr, path, nullptr,
            an::ui::processes::RegistrationDemosaic{.method = method,
                                                    .pattern = an::ui::bayerPatternChoiceFromText(pattern)});
    };
    const auto mentions = [](const an::Image<float>& image, std::string_view needle) {
        for (const auto& entry : image.metadata().provenance().entries())
        {
            if (std::string(entry.detail()).find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    };

    for (const auto& [method, ran] : {std::pair{an::DemosaicMethod::Rcd, "MarkesteijnThreePass"},
                                      std::pair{an::DemosaicMethod::Fast, "XTransFast"}})
    {
        SCOPED_TRACE(an::demosaicMethodName(method));
        const an::Result<an::Image<float>> light = read(method, an::ui::kBayerPatternFromImageText);
        ASSERT_TRUE(light.hasValue()) << light.status().message() << ": " << light.status().rootCause().message();
        const std::optional<an::ImageDescriptor> described = light->descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->sampleType(), an::SampleType::Float32);
        EXPECT_EQ(described->extent().channelCount(), 3u);
        EXPECT_FALSE(described->isMosaiced());
        EXPECT_TRUE(mentions(*light, ran));
    }

    const an::Result<an::Image<float>> named = read(an::DemosaicMethod::Rcd, an::ui::kBayerPatternRggbText);
    ASSERT_FALSE(named.hasValue());
    EXPECT_EQ(named.status().code(), an::ErrorCode::IncompatibleArguments);
    bool namesTheArrangement = false;
    for (an::Status cause = named.status(); cause.failed(); cause = cause.cause())
    {
        namesTheArrangement = namesTheArrangement || std::string(cause.message()).find("X-Trans") != std::string::npos;
    }
    EXPECT_TRUE(namesTheArrangement) << named.status().message();
}

TEST_F(LightsRegistrationTest, TheStageIsRefusedOutsideARun)
{
    const QString reference_path = writeLight(QStringLiteral("light_001"), starCatalogue(20u, 9u));
    useLights({reference_path});
    LightsRegistrationStage stage;
    CancellationToken token;
    ProgressReporter reporter;
    const StageResult result = stage.execute(m_manifest, token, reporter);
    EXPECT_FALSE(result.isSuccess());
    EXPECT_FALSE(QFileInfo::exists(metricsPath()));
}

TEST_F(LightsRegistrationTest, AStoppedRunReportsCancellation)
{
    const std::vector<FixtureStar> reference = starCatalogue(30u, 13u);
    const QString reference_path = writeLight(QStringLiteral("light_001"), reference, {}, 61u);
    const QString light_path =
        writeLight(QStringLiteral("light_002"), movedStars(reference, translation(2.0, 2.0)), {}, 62u);
    useLights({reference_path, light_path});
    m_manifest.registrationGroups = {m_group};
    CancellationToken token;
    token.requestCancel();
    LightsRegistrationStage stage;
    const StageResult result = m_run.run(stage, m_manifest, token);
    EXPECT_TRUE(result.isCancelled());
}

} // namespace
