// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Registration: the model a registration's settings state - a detection value the detector refuses run at the
/// nearest it takes, and said - a frame read in colour under its own filter array -
/// an X-Trans one included - its stars detected and matched to a reference by the engine, directly or through a frame
/// registered near it, the translation phase correlation measures, the canvas and the aligned frame, and the window
/// that registers its frames on the thread pool - each matched and aligned with its own plate distortion undone -
/// records them where its outputs are, aligns the included ones, reaches the settings dialog, the blink comparator and
/// the comet registration through the companions and persists its options.

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/WorkflowToolFiles.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an::tests::processes::workflow
{
namespace
{

using an::tests::processes::stars::makeStarField;
using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;

/// Where each frame's stars lie against the reference's.
constexpr std::array<std::pair<double, double>, 3> kShifts{{{0.0, 0.0}, {6.0, -4.0}, {-3.5, 7.25}}};

/// Each frame's extent, the sky's width, and how far from a frame's edges every star it holds lies.
constexpr std::uint32_t kWindowWidth = 320u;
constexpr std::uint32_t kWindowHeight = 240u;
constexpr double kSkyWidth = 600.0;
constexpr double kWindowEdge = 8.0;

/// A sky wider than a frame: round stars of a few amplitudes scattered apart from one another and from its edges, which
/// frames look onto at stated horizontal offsets.
[[nodiscard]] std::vector<PlantedStar> wideSky()
{
    constexpr std::size_t kCount = 120u;
    constexpr double kMargin = 12.0;
    constexpr double kSeparation = 12.0;
    std::uint32_t state = 0x9e3779b9u;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    std::vector<PlantedStar> sky;
    for (std::size_t attempt = 0u; attempt < kCount * 50u && sky.size() < kCount; ++attempt)
    {
        PlantedStar star;
        star.x = kMargin + static_cast<double>(next() % 100000u) / 100000.0 * (kSkyWidth - 2.0 * kMargin);
        star.y = kMargin + static_cast<double>(next() % 100000u) / 100000.0 * (kWindowHeight - 2.0 * kMargin);
        const bool crowded = std::any_of(sky.begin(), sky.end(), [&star](const PlantedStar& other) {
            return std::hypot(other.x - star.x, other.y - star.y) < kSeparation;
        });
        if (crowded)
        {
            continue;
        }
        star.amplitude = 0.3 + 0.1 * static_cast<double>(sky.size() % 5u);
        star.sigma = 1.5;
        sky.push_back(star);
    }
    return sky;
}

/// Whether a frame looking onto the sky at an offset holds a star, away from its edges.
[[nodiscard]] bool windowHolds(const PlantedStar& star, const double offset)
{
    return star.x - offset >= kWindowEdge && star.x - offset <= static_cast<double>(kWindowWidth) - kWindowEdge;
}

/// How many stars of the sky two frames at two offsets both hold.
[[nodiscard]] std::size_t sharedStars(const std::vector<PlantedStar>& sky, const double first, const double second)
{
    return static_cast<std::size_t>(std::count_if(sky.begin(), sky.end(), [first, second](const PlantedStar& star) {
        return windowHolds(star, first) && windowHolds(star, second);
    }));
}

/// Writes what a frame looking onto the sky at an offset sees, as a grey Float32 FITS file: every star it holds,
/// rendered at its pixel centres over the sky, and nothing of a star it does not.
[[nodiscard]] bool writeSkyWindow(const an::consumer::EngineSession& session, const QString& path,
                                  const std::vector<PlantedStar>& sky, const double offset)
{
    constexpr double kBackground = 0.05;
    std::vector<PlantedStar> held;
    for (const PlantedStar& star : sky)
    {
        if (windowHolds(star, offset))
        {
            PlantedStar moved = star;
            moved.x -= offset;
            held.push_back(moved);
        }
    }
    return writeFrame(session, path, kWindowWidth, kWindowHeight, 1u, an::SampleType::Float32,
                      [&held](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                          double value = kBackground;
                          for (const PlantedStar& star : held)
                          {
                              const double dx = (static_cast<double>(x) + 0.5) - star.x;
                              const double dy = (static_cast<double>(y) + 0.5) - star.y;
                              const double reach = star.sigma * 5.0;
                              if (std::abs(dx) <= reach && std::abs(dy) <= reach)
                              {
                                  value +=
                                      star.amplitude * std::exp(-(dx * dx + dy * dy) / (2.0 * star.sigma * star.sigma));
                              }
                          }
                          return value;
                      });
}

/// The centre of what rises above the sky within `radius` pixels of a position, in engine pixel coordinates: each
/// sample weighed by how far it rises above the sky; nothing when nothing does.
[[nodiscard]] std::optional<an::PixelCoordinate> centroidNear(const ReadBack& image, const double x, const double y,
                                                              const int radius, const double sky)
{
    const int column = static_cast<int>(std::floor(x));
    const int row = static_cast<int>(std::floor(y));
    double weight = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    for (int v = row - radius; v <= row + radius; ++v)
    {
        for (int u = column - radius; u <= column + radius; ++u)
        {
            if (u < 0 || v < 0 || u >= static_cast<int>(image.width) || v >= static_cast<int>(image.height))
            {
                continue;
            }
            const double rise =
                (std::max)(0.0, image.at(static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v)) - sky);
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
}

struct RegistrationWindow
{
    RegistrationWindow() : window(new ImageRegistrationProcess())
    {
        window->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    ~RegistrationWindow()
    {
        (void)pumpUntil([this]() { return window->isIdle(); });
        delete window;
        QCoreApplication::processEvents();
    }
    RegistrationWindow(const RegistrationWindow&) = delete;
    RegistrationWindow& operator=(const RegistrationWindow&) = delete;

    ImageRegistrationProcess* window;
};

class RegistrationTest : public WorkflowCase
{
protected:
    /// The reference and two frames of one field, each moved by its shift.
    [[nodiscard]] QStringList writeFrames(bool solved = false)
    {
        const StarField field = makeStarField();
        QStringList paths;
        for (std::size_t index = 0; index < kShifts.size(); ++index)
        {
            const QString path = temporaryPath(QStringLiteral("light_%1.fits").arg(index));
            EXPECT_TRUE(writeShiftedField(session(), path, field, kShifts[index].first, kShifts[index].second, solved));
            paths << path;
        }
        return paths;
    }

    [[nodiscard]] QString output() const
    {
        return temporaryPath(QStringLiteral("registered"));
    }

    /// Every frame read, then its stars detected in one admitted operation: a read admits an operation of its own,
    /// which the session admits only once the case holds none.
    [[nodiscard]] RegisteredFrameSet detected(const QStringList& paths, const RegistrationSettings& settings)
    {
        RegisteredFrameSet set;
        set.referencePath = paths.front();
        std::vector<std::pair<QString, an::Image<float>>> frames;
        for (const QString& path : paths)
        {
            an::Result<an::Image<float>> frame = an::ui::processes::readRegistrationFrame(session(), path, {}, {});
            EXPECT_TRUE(frame.hasValue()) << frame.status().message();
            if (frame.hasValue())
            {
                frames.emplace_back(path, std::move(*frame));
            }
        }
        an::Result<an::consumer::OperationScope> scope =
            session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
        EXPECT_TRUE(scope.hasValue());
        if (!scope.hasValue())
        {
            return set;
        }
        for (auto& [path, frame] : frames)
        {
            an::Result<an::ui::processes::FrameDetection> stars = an::ui::processes::detectRegistrationStars(
                scope->execution(), frame, an::ui::processes::registrationStarDetection(settings));
            EXPECT_TRUE(stars.hasValue()) << stars.status().message();
            if (stars.hasValue())
            {
                stars->frame.sourcePath = path;
                set.frames.emplace(path, std::move(stars->frame));
            }
        }
        return set;
    }
};

TEST(RegistrationModelTest, ALinearTransformationIsMatchedAndFittedAsItsKind)
{
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.minStarPairs = 7;
    for (const auto& [text, kind] :
         {std::pair{QStringLiteral("Similarity"), an::RegistrationTransformKind::Similarity},
          std::pair{QStringLiteral("Affine"), an::RegistrationTransformKind::Affine},
          std::pair{QStringLiteral("Homography"), an::RegistrationTransformKind::Projective}})
    {
        settings.transformation = text;
        const an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
        ASSERT_TRUE(model.hasValue()) << model.status().message();
        EXPECT_FALSE(model->phaseCorrelation);
        EXPECT_EQ(model->fit.linearKind, kind) << text.toStdString();
        EXPECT_EQ(model->minimumMatches, 7u);
    }
}

TEST(RegistrationModelTest, APolynomialStatesItsOrderAndAutoLetsTheFitChoose)
{
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Polynomial");
    settings.transformOrder = 3;
    an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    EXPECT_EQ(model->fit.polynomialPolicy, an::RegistrationPolynomialPolicy::StatedOrder);
    EXPECT_EQ(model->fit.polynomialOrder, 3u);

    settings.transformOrder = 7;
    EXPECT_EQ(an::ui::processes::registrationModelOf(settings).status().code(), an::ErrorCode::InvalidArgument);

    settings.transformMode = QStringLiteral("Auto");
    model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    EXPECT_EQ(model->fit.polynomialPolicy, an::RegistrationPolynomialPolicy::AutomaticOrder);
}

TEST(RegistrationModelTest, PhaseCorrelationIsASimilarityAndAnUnknownMethodIsRefused)
{
    RegistrationSettings settings;
    settings.registrationMethod = QStringLiteral("phase_correlation");
    an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    EXPECT_TRUE(model->phaseCorrelation);
    EXPECT_EQ(model->fit.linearKind, an::RegistrationTransformKind::Similarity);

    settings.registrationMethod = QStringLiteral("optical_flow");
    EXPECT_EQ(an::ui::processes::registrationModelOf(settings).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(RegistrationModelTest, TheAlignmentsChoicesAreReadAsTheIntegrationReadsThem)
{
    using an::ui::processes::alignmentInterpolationOf;
    using an::ui::processes::registrationCanvasModeOf;
    EXPECT_EQ(registrationCanvasModeOf(QStringLiteral("standard")), an::RegistrationCanvasMode::Reference);
    EXPECT_EQ(registrationCanvasModeOf(QStringLiteral("Mosaic")), an::RegistrationCanvasMode::Union);
    EXPECT_EQ(registrationCanvasModeOf(QStringLiteral("intersection")), an::RegistrationCanvasMode::Intersection);

    ASSERT_TRUE(alignmentInterpolationOf(QStringLiteral("Auto")).hasValue());
    EXPECT_EQ(*alignmentInterpolationOf(QStringLiteral("Auto")), an::KernelInterpolation::Lanczos4);
    EXPECT_EQ(*alignmentInterpolationOf(QStringLiteral("Bilinear")), an::KernelInterpolation::Linear);
    EXPECT_EQ(*alignmentInterpolationOf(QStringLiteral("Nearest Neighbor")), an::KernelInterpolation::Nearest);
    EXPECT_FALSE(alignmentInterpolationOf(QStringLiteral("Sinc")).hasValue());

    EXPECT_EQ(an::ui::processes::alignedOutputPath(QStringLiteral("/in/m31.light.fits"), QStringLiteral("/out")),
              QStringLiteral("/out/m31.light_reg.fits"));
}

// A frame's stars are corrected by its distortion's forward polynomials, which a model of inverse polynomials alone
// does not have: the registration reads no distortion from such a solution, and neither the canvas nor the warp undoes
// one, where the inverse polynomials were undone under a transform solved over the stars as they were detected.
TEST(RegistrationModelTest, InversePolynomialsAloneAreNoDistortionTheRegistrationUndoes)
{
    const an::PixelCoordinate centre{.x = 160.0, .y = 120.0};
    an::SipPolynomial bend;
    bend.order = 2u;
    bend.coefficients[2][0] = 1.0e-4;
    const an::Result<an::SipDistortion> inverseOnly =
        an::SipDistortion::create(an::SipDistortionRequest{.referencePixel = centre, .inverseX = bend});
    const an::Result<an::SipDistortion> forward =
        an::SipDistortion::create(an::SipDistortionRequest{.referencePixel = centre, .forwardX = bend});
    ASSERT_TRUE(inverseOnly.hasValue()) << inverseOnly.status().message();
    ASSERT_TRUE(forward.hasValue()) << forward.status().message();
    const auto solved = [&centre](const an::SipDistortion& distortion) {
        constexpr double kScale = 2.4 / 3600.0;
        return an::WcsSolution::create(
            an::WcsSolutionRequest{.referencePixel = centre,
                                   .referencePoint = {.rightAscensionDegrees = 83.6, .declinationDegrees = 22.0},
                                   .linear = {.cd11 = -kScale, .cd12 = 0.0, .cd21 = 0.0, .cd22 = kScale},
                                   .imageWidth = 320u,
                                   .imageHeight = 240u,
                                   .distortion = distortion});
    };
    const an::Result<an::WcsSolution> inverseSolution = solved(*inverseOnly);
    const an::Result<an::WcsSolution> forwardSolution = solved(*forward);
    ASSERT_TRUE(inverseSolution.hasValue()) << inverseSolution.status().message();
    ASSERT_TRUE(forwardSolution.hasValue()) << forwardSolution.status().message();
    EXPECT_FALSE(an::ui::processes::registrationDistortionOf(*inverseSolution).has_value());
    const std::optional<an::SipDistortion> read = an::ui::processes::registrationDistortionOf(*forwardSolution);
    ASSERT_TRUE(read.has_value());
    EXPECT_DOUBLE_EQ(read->forwardX().coefficients[2][0], 1.0e-4);

    // The canvas maps an edge through forward polynomials alone.
    const std::vector<an::RegistrationTransform> identity{an::RegistrationTransform{}};
    const an::Result<an::RegistrationCanvas> plain =
        an::ui::processes::planAlignmentCanvas(identity, 0u, 320u, 240u, an::RegistrationCanvasMode::Union);
    const an::Result<an::RegistrationCanvas> inverted = an::ui::processes::planAlignmentCanvas(
        identity, 0u, 320u, 240u, an::RegistrationCanvasMode::Union, std::vector<an::SipDistortion>{*inverseOnly});
    const an::Result<an::RegistrationCanvas> bent = an::ui::processes::planAlignmentCanvas(
        identity, 0u, 320u, 240u, an::RegistrationCanvasMode::Union, std::vector<an::SipDistortion>{*forward});
    ASSERT_TRUE(plain.hasValue() && inverted.hasValue() && bent.hasValue());
    EXPECT_EQ(*inverted, *plain);
    EXPECT_NE(*bent, *plain);
}

TEST(RegistrationModelTest, TheDetectionKeepsAsManyStarsAsTheMostPairsTheSettingsAllow)
{
    RegistrationSettings settings;
    settings.maxStarPairs = 321;
    const an::StarDetectionRequest request = an::ui::processes::registrationStarDetection(settings);
    EXPECT_EQ(request.maximumStars, 321u);
}

// The detector refuses a search radius above thirty-two and a roundness threshold or a Moffat exponent that is not
// positive, so a radius of fifty or a roundness threshold of zero - both of which the dialog offered - or an exponent
// of zero failed every frame's detection. Each runs at the nearest value the detector takes, and one sentence says so.
TEST(RegistrationModelTest, AValueTheDetectorRefusesRunsAtTheNearestValueItTakesAndIsSaid)
{
    using an::ui::processes::registrationStarDetection;
    using an::ui::processes::registrationStarDetectionAdjustment;
    using ImageIntegration::RegistrationSettingsUtils::kLargestStarSearchRadius;
    using ImageIntegration::RegistrationSettingsUtils::kSmallestMoffatBeta;
    using ImageIntegration::RegistrationSettingsUtils::kSmallestStarRoundness;
    RegistrationSettings settings;
    settings.radius = 50;
    settings.roundnessThreshold = 0.0;
    settings.profileType = QStringLiteral("Moffat");
    settings.minimumBeta = 0.0;
    const an::StarDetectionRequest request = registrationStarDetection(settings);
    EXPECT_EQ(request.searchRadius, static_cast<std::uint32_t>(kLargestStarSearchRadius));
    EXPECT_DOUBLE_EQ(request.minimumRoundness, kSmallestStarRoundness);
    EXPECT_DOUBLE_EQ(request.minimumBeta, kSmallestMoffatBeta);
    const QString said = registrationStarDetectionAdjustment(settings);
    for (const QString& part : {QStringLiteral("radius 50 as 32"), QStringLiteral("roundness threshold 0.00 as 0.01"),
                                QStringLiteral("minimum beta 0.00 as 0.05")})
    {
        EXPECT_TRUE(said.contains(part)) << said.toStdString() << " does not say " << part.toStdString();
    }

    // The Gaussian profile reads no exponent: one the detector refuses still runs at the floor, and is not said.
    settings.profileType = QStringLiteral("Gaussian");
    EXPECT_DOUBLE_EQ(registrationStarDetection(settings).minimumBeta, kSmallestMoffatBeta);
    EXPECT_FALSE(registrationStarDetectionAdjustment(settings).contains(QStringLiteral("beta")));

    // A value the detector takes runs as it is, and nothing is said.
    RegistrationSettings taken;
    taken.radius = kLargestStarSearchRadius;
    taken.roundnessThreshold = kSmallestStarRoundness;
    taken.profileType = QStringLiteral("Moffat");
    taken.minimumBeta = 0.03;
    const an::StarDetectionRequest held = registrationStarDetection(taken);
    EXPECT_EQ(held.searchRadius, static_cast<std::uint32_t>(kLargestStarSearchRadius));
    EXPECT_DOUBLE_EQ(held.minimumRoundness, kSmallestStarRoundness);
    EXPECT_DOUBLE_EQ(held.minimumBeta, 0.03);
    EXPECT_TRUE(registrationStarDetectionAdjustment(taken).isEmpty())
        << registrationStarDetectionAdjustment(taken).toStdString();
}

TEST_F(RegistrationTest, MatchedFramesCarryTheTransformOntoTheReference)
{
    const QStringList frames = writeFrames();
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.transformation = QStringLiteral("Affine");
    RegisteredFrameSet set = detected(frames, settings);
    ASSERT_EQ(set.frames.size(), 3u);
    for (const auto& [path, frame] : set.frames)
    {
        EXPECT_GE(frame.metrics.stars, 10) << path.toStdString();
        EXPECT_GT(frame.metrics.fwhm, 1.0);
        EXPECT_GT(frame.metrics.snr, 0.0);
    }
    const an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue());
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    std::size_t reported = 0u;
    const an::Status matched = an::ui::processes::matchRegistrationStars(
        scope->execution(), set, frames.front(), *model, 320u, 240u, [&reported](std::size_t done, std::size_t total) {
            reported = done;
            EXPECT_EQ(total, 3u);
        });
    ASSERT_TRUE(matched.ok()) << matched.message();
    EXPECT_EQ(reported, 3u);
    for (std::size_t index = 0; index < kShifts.size(); ++index)
    {
        const RegisteredFrame& frame = set.frames.at(frames[static_cast<qsizetype>(index)]);
        ASSERT_FALSE(frame.failed) << index;
        ASSERT_TRUE(frame.transform.has_value()) << index;
        // A star moved by the shift maps back to where the reference holds it.
        EXPECT_NEAR(frame.transform->translation().x, -kShifts[index].first, 0.05) << index;
        EXPECT_NEAR(frame.transform->translation().y, -kShifts[index].second, 0.05) << index;
        EXPECT_LT(frame.metrics.astrometric_rms_pixels, 0.1) << index;
    }
}

// Settings the detector refuses as they are stated - a radius of fifty, a roundness threshold and a Moffat exponent of
// zero - failed every frame's detection; run at the nearest values it takes, every frame's stars are found.
TEST_F(RegistrationTest, EveryFrameIsDetectedUnderValuesTheDetectorRefusesAsTheyAreStated)
{
    RegistrationSettings settings;
    settings.radius = 50;
    settings.roundnessThreshold = 0.0;
    settings.profileType = QStringLiteral("Moffat");
    settings.minimumBeta = 0.0;
    const RegisteredFrameSet set = detected(writeFrames(), settings);
    EXPECT_EQ(set.frames.size(), kShifts.size());
    for (const auto& [path, frame] : set.frames)
    {
        EXPECT_FALSE(frame.stars.empty()) << path.toStdString();
    }
}

TEST_F(RegistrationTest, AFrameWithTooFewMatchesFails)
{
    const QStringList frames = writeFrames();
    // A frame of nothing but sky.
    const QString empty = temporaryPath(QStringLiteral("sky.fits"));
    ASSERT_TRUE(writeFlatFrame(session(), empty, 320u, 240u, 1u, 0.05, an::SampleType::Float32));
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.transformation = QStringLiteral("Similarity");
    RegisteredFrameSet set = detected(QStringList{frames.front(), empty}, settings);
    ASSERT_EQ(set.frames.size(), 2u);
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Status matched = an::ui::processes::matchRegistrationStars(
        scope->execution(), set, frames.front(), *an::ui::processes::registrationModelOf(settings), 320u, 240u, {});
    ASSERT_TRUE(matched.ok()) << matched.message();
    EXPECT_TRUE(set.frames.at(empty).failed);
    EXPECT_FALSE(set.frames.at(empty).transform.has_value());
    EXPECT_FALSE(set.frames.at(frames.front()).failed);
}

TEST_F(RegistrationTest, AReferenceOutsideTheSetIsRefused)
{
    const QStringList frames = writeFrames();
    RegisteredFrameSet set = detected(frames, RegistrationSettings{});
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Status matched = an::ui::processes::matchRegistrationStars(
        scope->execution(), set, temporaryPath(QStringLiteral("elsewhere.fits")),
        *an::ui::processes::registrationModelOf(RegistrationSettings{}), 320u, 240u, {});
    EXPECT_EQ(matched.code(), an::ErrorCode::InvalidArgument);
}

// Four frames look onto a sky wider than one, each the one before moved by two fifths of a frame, so each shares about
// half of its stars with the one before - well over the ten pairs a match needs, which is accepted on its pairs at any
// share of its candidates. The reference holds fewer than ten of the third frame's stars and none of the fourth's. The
// third frame is matched to the second, whose matched stars the second's transform carries onto the reference, and the
// fourth to the third registered so - a chained registration.
TEST_F(RegistrationTest, AFrameTheReferenceHoldsTooFewStarsOfIsRegisteredThroughAFrameRegisteredNearIt)
{
    const std::vector<PlantedStar> sky = wideSky();
    constexpr std::array<double, 4> kOffsets{0.0, 130.0, 260.0, 390.0};
    for (std::size_t index = 1; index < kOffsets.size(); ++index)
    {
        ASSERT_GE(sharedStars(sky, kOffsets[index - 1], kOffsets[index]), 25u) << index;
    }
    ASSERT_LT(sharedStars(sky, kOffsets.front(), kOffsets[2]), 10u)
        << "the reference holds enough of the third frame's stars to match it";
    ASSERT_LT(sharedStars(sky, kOffsets.front(), kOffsets.back()), 5u)
        << "the reference holds enough of the far frame's stars to match it";
    QStringList frames;
    for (std::size_t index = 0; index < kOffsets.size(); ++index)
    {
        const QString path = temporaryPath(QStringLiteral("window_%1.fits").arg(index));
        ASSERT_TRUE(writeSkyWindow(session(), path, sky, kOffsets[index]));
        frames << path;
    }
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.transformation = QStringLiteral("Affine");
    RegisteredFrameSet set = detected(frames, settings);
    ASSERT_EQ(set.frames.size(), kOffsets.size());
    const an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue());
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Status matched = an::ui::processes::matchRegistrationStars(scope->execution(), set, frames.front(),
                                                                         *model, kWindowWidth, kWindowHeight, {});
    ASSERT_TRUE(matched.ok()) << matched.message();
    for (std::size_t index = 1; index < kOffsets.size(); ++index)
    {
        const RegisteredFrame& frame = set.frames.at(frames[static_cast<qsizetype>(index)]);
        ASSERT_FALSE(frame.failed) << index;
        ASSERT_TRUE(frame.transform.has_value()) << index;
        // Each star the frame holds maps onto the reference where the sky puts it.
        for (const PlantedStar& star : sky)
        {
            if (!windowHolds(star, kOffsets[index]))
            {
                continue;
            }
            const an::Result<an::PixelCoordinate> mapped = an::mapRegistrationPoint(
                *frame.transform, an::PixelCoordinate{.x = star.x - kOffsets[index], .y = star.y});
            ASSERT_TRUE(mapped.hasValue());
            EXPECT_NEAR(mapped->x, star.x, 0.1) << index;
            EXPECT_NEAR(mapped->y, star.y, 0.1) << index;
        }
    }
}

// A frame looking onto the sky two thirds of a frame from the reference shares about a third of its stars with it -
// twice the ten pairs a match needs - and has no other frame to be matched through. Nearly every star it holds pairs
// with one of the reference's, so its pairs are about a third of the candidates; a match of ten candidate pairs or more
// is accepted on its pairs, so the frame is registered by the stars it shares.
TEST_F(RegistrationTest, AFrameSharingAThirdOfItsStarsWithTheReferenceIsRegisteredByTheStarsItShares)
{
    const std::vector<PlantedStar> sky = wideSky();
    constexpr double kOffset = 192.0;
    const std::size_t shared = sharedStars(sky, 0.0, kOffset);
    const auto held = static_cast<std::size_t>(
        std::count_if(sky.begin(), sky.end(), [](const PlantedStar& star) { return windowHolds(star, kOffset); }));
    ASSERT_GE(shared, 20u) << "the frame shares too few stars with the reference";
    ASSERT_LT(shared * 5u, held * 2u) << "the frame shares two fifths of its stars or more with the reference";
    QStringList frames;
    for (const double offset : {0.0, kOffset})
    {
        const QString path = temporaryPath(QStringLiteral("window_%1.fits").arg(offset));
        ASSERT_TRUE(writeSkyWindow(session(), path, sky, offset));
        frames << path;
    }
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.transformation = QStringLiteral("Affine");
    RegisteredFrameSet set = detected(frames, settings);
    ASSERT_EQ(set.frames.size(), 2u);
    const an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue());
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Status matched = an::ui::processes::matchRegistrationStars(scope->execution(), set, frames.front(),
                                                                         *model, kWindowWidth, kWindowHeight, {});
    ASSERT_TRUE(matched.ok()) << matched.message();
    const RegisteredFrame& frame = set.frames.at(frames.back());
    ASSERT_FALSE(frame.failed) << "the frame was refused though it shares more stars than a match needs";
    ASSERT_TRUE(frame.transform.has_value());
    // Each star the frame holds maps onto the reference where the sky puts it.
    for (const PlantedStar& star : sky)
    {
        if (!windowHolds(star, kOffset))
        {
            continue;
        }
        const an::Result<an::PixelCoordinate> mapped =
            an::mapRegistrationPoint(*frame.transform, an::PixelCoordinate{.x = star.x - kOffset, .y = star.y});
        ASSERT_TRUE(mapped.hasValue());
        EXPECT_NEAR(mapped->x, star.x, 0.1);
        EXPECT_NEAR(mapped->y, star.y, 0.1);
    }
}

// Nine stars, and a frame of the same nine moved together but for three, each moved a further eight pixels its own
// way: every star pairs with its own, and six of the nine pairs fit one transform. Six pairs are as many as the
// settings ask, but among so few pairs a few spurious ones can fit one transform, so a match of fewer than ten
// candidate pairs keeps four fifths of them: the frame is refused.
TEST_F(RegistrationTest, ASmallMatchIsRefusedWhenUnderFourFifthsOfItsPairsFitItsTransform)
{
    std::vector<PlantedStar> reference;
    for (const auto& [x, y] : std::array<std::pair<double, double>, 9>{{{40.0, 40.0},
                                                                        {160.0, 30.0},
                                                                        {280.0, 45.0},
                                                                        {70.0, 120.0},
                                                                        {170.0, 110.0},
                                                                        {265.0, 130.0},
                                                                        {45.0, 200.0},
                                                                        {150.0, 205.0},
                                                                        {285.0, 195.0}}})
    {
        reference.push_back(PlantedStar{.x = x, .y = y, .amplitude = 0.5, .sigma = 1.5});
    }
    std::vector<PlantedStar> moved = reference;
    for (PlantedStar& star : moved)
    {
        star.x += 6.0;
        star.y -= 4.0;
    }
    moved[1].x += 8.0;
    moved[5].y += 8.0;
    moved[6].x -= 8.0 * std::numbers::sqrt2 / 2.0;
    moved[6].y -= 8.0 * std::numbers::sqrt2 / 2.0;
    const QString referencePath = temporaryPath(QStringLiteral("few_reference.fits"));
    const QString movedPath = temporaryPath(QStringLiteral("few_moved.fits"));
    ASSERT_TRUE(writeSkyWindow(session(), referencePath, reference, 0.0));
    ASSERT_TRUE(writeSkyWindow(session(), movedPath, moved, 0.0));
    RegistrationSettings settings;
    settings.transformMode = QStringLiteral("Linear");
    settings.transformation = QStringLiteral("Affine");
    settings.minStarPairs = 6;
    RegisteredFrameSet set = detected(QStringList{referencePath, movedPath}, settings);
    ASSERT_EQ(set.frames.size(), 2u);
    ASSERT_EQ(set.frames.at(referencePath).stars.size(), reference.size());
    ASSERT_EQ(set.frames.at(movedPath).stars.size(), moved.size());
    const an::Result<an::ui::processes::RegistrationModel> model = an::ui::processes::registrationModelOf(settings);
    ASSERT_TRUE(model.hasValue());
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Status matched = an::ui::processes::matchRegistrationStars(scope->execution(), set, referencePath, *model,
                                                                         kWindowWidth, kWindowHeight, {});
    ASSERT_TRUE(matched.ok()) << matched.message();
    const RegisteredFrame& frame = set.frames.at(movedPath);
    EXPECT_TRUE(frame.failed) << "a small match was accepted on its pairs though a third of them fit no transform";
    EXPECT_FALSE(frame.transform.has_value());
    EXPECT_FALSE(set.frames.at(referencePath).failed);
}

// A star match of fewer than ten candidate pairs is held to a share of inliers among them - four fifths for a
// registration's, three fifths for the chromatic-aberration correction's - unless either field holds a thousand stars
// or more; every other match is accepted on its pairs, never fewer than five.
TEST(RegistrationModelTest, AStarMatchIsHeldToAShareOfItsCandidatePairsOnlyWhenItIsSmall)
{
    using an::ui::processes::checkRegistrationStarMatch;
    using an::ui::processes::kChromaticAberrationSmallMatchInlierFraction;
    using an::ui::processes::kRegistrationSmallMatchInlierFraction;
    const auto match = [](std::uint32_t candidates, std::uint32_t inliers, std::uint32_t source = 60u,
                          std::uint32_t reference = 60u) {
        an::StarAlignmentQuality quality;
        quality.sourcePoints = source;
        quality.referencePoints = reference;
        quality.candidateMatches = candidates;
        quality.inlierMatches = inliers;
        quality.rejectedOutliers = candidates - inliers;
        quality.inlierFraction = static_cast<double>(inliers) / static_cast<double>(candidates);
        return quality;
    };
    // A match of ten candidate pairs or more is accepted on its pairs, whatever share of the candidates they are.
    EXPECT_TRUE(checkRegistrationStarMatch(match(57u, 20u), 10u, kRegistrationSmallMatchInlierFraction).ok());
    EXPECT_TRUE(checkRegistrationStarMatch(match(10u, 5u), 5u, kRegistrationSmallMatchInlierFraction).ok());
    EXPECT_TRUE(checkRegistrationStarMatch(match(40u, 12u), 6u, kChromaticAberrationSmallMatchInlierFraction).ok());
    // ... but on no fewer pairs than asked, and never on fewer than five.
    EXPECT_EQ(checkRegistrationStarMatch(match(57u, 9u), 10u, kRegistrationSmallMatchInlierFraction).code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(checkRegistrationStarMatch(match(9u, 4u), 3u, kRegistrationSmallMatchInlierFraction).code(),
              an::ErrorCode::EmptySelection);
    // A smaller match keeps four fifths of its candidates as inliers, or three fifths in the correction.
    EXPECT_TRUE(checkRegistrationStarMatch(match(9u, 8u), 6u, kRegistrationSmallMatchInlierFraction).ok());
    EXPECT_EQ(checkRegistrationStarMatch(match(9u, 7u), 6u, kRegistrationSmallMatchInlierFraction).code(),
              an::ErrorCode::EmptySelection);
    EXPECT_TRUE(checkRegistrationStarMatch(match(9u, 6u), 6u, kChromaticAberrationSmallMatchInlierFraction).ok());
    EXPECT_EQ(checkRegistrationStarMatch(match(9u, 5u), 5u, kChromaticAberrationSmallMatchInlierFraction).code(),
              an::ErrorCode::EmptySelection);
    // A field of a thousand stars or more, on either side, exempts a small match.
    EXPECT_TRUE(checkRegistrationStarMatch(match(9u, 6u, 1000u, 60u), 6u, kRegistrationSmallMatchInlierFraction).ok());
    EXPECT_TRUE(checkRegistrationStarMatch(match(9u, 6u, 60u, 1000u), 6u, kRegistrationSmallMatchInlierFraction).ok());
    EXPECT_EQ(checkRegistrationStarMatch(match(9u, 6u, 999u, 999u), 6u, kRegistrationSmallMatchInlierFraction).code(),
              an::ErrorCode::EmptySelection);
}

TEST(RegistrationModelTest, AutoReadsTheImagesOwnFilterArrayAndAStatedPatternIsKept)
{
    RegistrationSettings settings;
    for (const QString& text : {QStringLiteral("Auto"), QStringLiteral("auto"), QStringLiteral(" AUTO ")})
    {
        settings.cfaPattern = text;
        const an::ui::processes::RegistrationDemosaic demosaic = an::ui::processes::registrationDemosaicOf(settings);
        EXPECT_FALSE(demosaic.pattern.stated.has_value()) << text.toStdString();
        EXPECT_TRUE(demosaic.pattern.known) << text.toStdString();
    }
    settings.cfaPattern = QStringLiteral("grbg");
    const an::ui::processes::RegistrationDemosaic stated = an::ui::processes::registrationDemosaicOf(settings);
    ASSERT_TRUE(stated.pattern.stated.has_value());
    EXPECT_EQ(*stated.pattern.stated, an::CfaPattern::Grbg);
}

// Image Registration reads an X-Trans frame in colour by its method's tabulated counterpart, since the engine refuses a
// Bayer method over an X-Trans table, and refuses a named two-by-two pattern over one by name.
TEST_F(RegistrationTest, AnXTransFrameIsReadInColourByTheTabulatedCounterpartOfItsMethod)
{
    const QString path = temporaryPath(QStringLiteral("xtrans.tif"));
    ASSERT_TRUE(an::tests::viewer::writeXTransTiff(session(), path));
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
    RegistrationSettings settings;
    settings.cfaPattern = QStringLiteral("Auto");
    for (const auto& [method, ran] : {std::pair{"rcd", "MarkesteijnThreePass"},
                                      std::pair{"vng4", "MarkesteijnThreePass"}, std::pair{"fast", "XTransFast"}})
    {
        SCOPED_TRACE(method);
        settings.debayerMethod = QString::fromLatin1(method);
        an::Result<an::Image<float>> frame = an::ui::processes::readRegistrationFrame(
            session(), path, an::ui::processes::registrationDemosaicOf(settings), {});
        ASSERT_TRUE(frame.hasValue()) << frame.status().message();
        const std::optional<an::ImageDescriptor> described = frame->descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->sampleType(), an::SampleType::Float32);
        EXPECT_EQ(described->extent().channelCount(), 3u);
        EXPECT_FALSE(described->isMosaiced());
        EXPECT_TRUE(mentions(*frame, ran));
    }

    settings.debayerMethod = QStringLiteral("rcd");
    settings.cfaPattern = QStringLiteral("grbg");
    const an::Result<an::Image<float>> named = an::ui::processes::readRegistrationFrame(
        session(), path, an::ui::processes::registrationDemosaicOf(settings), {});
    ASSERT_FALSE(named.hasValue());
    EXPECT_EQ(named.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NE(std::string(named.status().message()).find("X-Trans"), std::string::npos) << named.status().message();
}

TEST_F(RegistrationTest, StarsOfAColourFrameAreSearchedInItsGreenChannel)
{
    // The stars lie in the green channel alone; the red and blue hold the sky.
    const StarField field = makeStarField(320u, 240u, 1u);
    const QString path = temporaryPath(QStringLiteral("green_only.fits"));
    ASSERT_TRUE(writeFrame(session(), path, field.width, field.height, 3u, an::SampleType::Float32,
                           [&field](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                               return c == 1u ? static_cast<double>(field.value(x, y, 0u)) : field.background;
                           }));
    const RegisteredFrameSet set = detected(QStringList{path}, RegistrationSettings{});
    ASSERT_EQ(set.frames.size(), 1u);
    EXPECT_GE(set.frames.at(path).stars.size(), 10u);
}

TEST_F(RegistrationTest, ACorrectionMovesTheRedAndBlueOntoTheGreenInMemory)
{
    // The same stars in every channel, the red lying a pixel and a half to one side of the green, the blue the other.
    const StarField field = makeStarField(320u, 240u, 1u);
    const auto shift = [](std::uint32_t channel) {
        return channel == 0u ? std::pair{1.5, 1.0} : (channel == 2u ? std::pair{-1.5, -1.0} : std::pair{0.0, 0.0});
    };
    const auto sample = [&field, &shift](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        const auto [dx, dy] = shift(c);
        double value = field.background;
        for (const PlantedStar& star : field.stars)
        {
            const double ddx = (static_cast<double>(x) + 0.5) - (star.x + dx);
            const double ddy = (static_cast<double>(y) + 0.5) - (star.y + dy);
            const double reach = star.sigma * 5.0;
            if (std::abs(ddx) > reach || std::abs(ddy) > reach)
            {
                continue;
            }
            value += star.amplitude * std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * star.sigma * star.sigma));
        }
        return value;
    };
    const QString path = temporaryPath(QStringLiteral("fringed.fits"));
    ASSERT_TRUE(writeFrame(session(), path, field.width, field.height, 3u, an::SampleType::Float32, sample));
    an::Result<an::Image<float>> frame = an::ui::processes::readRegistrationFrame(session(), path, {}, {});
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    // How far a channel's plane lies from the green one's.
    const auto distanceToGreen = [](const an::Image<float>& image, std::uint32_t channel) {
        const auto view = image.view();
        double sum = 0.0;
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            const auto plane = view->row(channel, y);
            const auto green = view->row(1u, y);
            for (std::size_t x = 0u; x < plane->size(); ++x)
            {
                const double difference = static_cast<double>((*plane)[x]) - static_cast<double>((*green)[x]);
                sum += difference * difference;
            }
        }
        return sum;
    };
    const double redBefore = distanceToGreen(*frame, 0u);
    const double blueBefore = distanceToGreen(*frame, 2u);

    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    RegistrationSettings::ChromaticAberrationSettings correction;
    correction.enabled = true;
    correction.edgeFraction = 0.0;
    correction.minMatchedStars = 3;
    const an::Result<an::ui::processes::ChromaticCorrection> done = an::ui::processes::correctChromaticAberration(
        scope->engine(), scope->execution(), *frame, correction,
        an::ui::processes::registrationStarDetection(RegistrationSettings{}));
    ASSERT_TRUE(done.hasValue()) << done.status().message();
    EXPECT_TRUE(done->redApplied);
    EXPECT_TRUE(done->blueApplied);
    // The frame the caller holds is the corrected one.
    EXPECT_LT(distanceToGreen(*frame, 0u), 0.2 * redBefore);
    EXPECT_LT(distanceToGreen(*frame, 2u), 0.2 * blueBefore);
}

// The red channel holds a dozen of the green channel's stars, moved a pixel and a half by the aberration, and half as
// many again of its own, which the green does not see; the green holds as many of its own. Every red star pairs with a
// green one, so the dozen are two fifths of the candidate pairs - twice the six the settings ask. A match of ten
// candidate pairs or more is accepted on its pairs, and the red channel is moved onto the green one.
TEST_F(RegistrationTest, AChannelSharingUnderHalfOfItsStarsWithTheGreenIsStillCorrected)
{
    constexpr std::size_t kShared = 12u;
    constexpr std::size_t kOwn = 18u;
    std::vector<PlantedStar> stars;
    std::uint32_t state = 0x51ed270bu;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (std::size_t attempt = 0u; attempt < 2000u && stars.size() < kShared + 2u * kOwn; ++attempt)
    {
        PlantedStar star;
        star.x = 16.0 + static_cast<double>(next() % 100000u) / 100000.0 * (320.0 - 32.0);
        star.y = 16.0 + static_cast<double>(next() % 100000u) / 100000.0 * (240.0 - 32.0);
        const bool crowded = std::any_of(stars.begin(), stars.end(), [&star](const PlantedStar& other) {
            return std::hypot(other.x - star.x, other.y - star.y) < 14.0;
        });
        if (!crowded)
        {
            star.amplitude = 0.3 + 0.1 * static_cast<double>(stars.size() % 5u);
            star.sigma = 1.5;
            stars.push_back(star);
        }
    }
    ASSERT_EQ(stars.size(), kShared + 2u * kOwn);
    // The first dozen in every channel, the next eighteen in the green and blue ones, the last eighteen in the red one.
    const auto holds = [](std::uint32_t channel, std::size_t index) {
        return index < kShared || (channel == 0u ? index >= kShared + kOwn : index < kShared + kOwn);
    };
    const auto shift = [](std::uint32_t channel) {
        return channel == 0u ? std::pair{1.5, 1.0} : (channel == 2u ? std::pair{-1.5, -1.0} : std::pair{0.0, 0.0});
    };
    constexpr double kSky = 0.05;
    const auto sample = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        const auto [dx, dy] = shift(c);
        double value = kSky;
        for (std::size_t index = 0u; index < stars.size(); ++index)
        {
            if (!holds(c, index))
            {
                continue;
            }
            const PlantedStar& star = stars[index];
            const double ddx = (static_cast<double>(x) + 0.5) - (star.x + dx);
            const double ddy = (static_cast<double>(y) + 0.5) - (star.y + dy);
            if (std::abs(ddx) <= star.sigma * 5.0 && std::abs(ddy) <= star.sigma * 5.0)
            {
                value += star.amplitude * std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * star.sigma * star.sigma));
            }
        }
        return value;
    };
    const QString path = temporaryPath(QStringLiteral("own_red_stars.fits"));
    ASSERT_TRUE(writeFrame(session(), path, 320u, 240u, 3u, an::SampleType::Float32, sample));
    an::Result<an::Image<float>> frame = an::ui::processes::readRegistrationFrame(session(), path, {}, {});
    ASSERT_TRUE(frame.hasValue()) << frame.status().message();

    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    RegistrationSettings::ChromaticAberrationSettings correction;
    correction.enabled = true;
    correction.edgeFraction = 0.0;
    const an::Result<an::ui::processes::ChromaticCorrection> done = an::ui::processes::correctChromaticAberration(
        scope->engine(), scope->execution(), *frame, correction,
        an::ui::processes::registrationStarDetection(RegistrationSettings{}));
    ASSERT_TRUE(done.hasValue()) << done.status().message();
    ASSERT_TRUE(done->redApplied) << "the red channel was refused for the share of its stars the green holds";
    EXPECT_TRUE(done->blueApplied);
    EXPECT_GE(done->redMatches, kShared - 1u);

    // The centre of what rises above the sky in a channel, in the box about a position.
    const auto centreNear = [&frame](std::uint32_t channel, double x, double y) {
        const auto view = std::as_const(*frame).view();
        constexpr int kRadius = 3;
        double weight = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        for (int v = static_cast<int>(y) - kRadius; v <= static_cast<int>(y) + kRadius; ++v)
        {
            const auto row = view->row(channel, static_cast<std::uint32_t>(v));
            for (int u = static_cast<int>(x) - kRadius; u <= static_cast<int>(x) + kRadius; ++u)
            {
                const double rise = (std::max)(0.0, static_cast<double>((*row)[static_cast<std::size_t>(u)]) - kSky);
                weight += rise;
                sumX += rise * (static_cast<double>(u) + 0.5);
                sumY += rise * (static_cast<double>(v) + 0.5);
            }
        }
        return std::pair{sumX / weight, sumY / weight};
    };
    // Each star the red channel shares with the green now lies where the green holds it.
    for (std::size_t index = 0u; index < kShared; ++index)
    {
        const auto [redX, redY] = centreNear(0u, stars[index].x, stars[index].y);
        const auto [greenX, greenY] = centreNear(1u, stars[index].x, stars[index].y);
        EXPECT_NEAR(redX, greenX, 0.1) << index;
        EXPECT_NEAR(redY, greenY, 0.1) << index;
    }
}

TEST_F(RegistrationTest, PhaseCorrelationMeasuresTheTranslation)
{
    const QStringList frames = writeFrames();
    an::Result<an::Image<float>> reference = an::ui::processes::readRegistrationFrame(session(), frames[0], {}, {});
    an::Result<an::Image<float>> moved = an::ui::processes::readRegistrationFrame(session(), frames[1], {}, {});
    ASSERT_TRUE(reference.hasValue() && moved.hasValue());
    an::Result<an::consumer::OperationScope> scope =
        session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch});
    ASSERT_TRUE(scope.hasValue());
    const an::Result<an::RegistrationTransform> transform =
        an::ui::processes::phaseCorrelationTransform(scope->execution(), *reference, *moved);
    ASSERT_TRUE(transform.hasValue()) << transform.status().message();
    EXPECT_NEAR(transform->translation().x, -kShifts[1].first, 0.25);
    EXPECT_NEAR(transform->translation().y, -kShifts[1].second, 0.25);
}

TEST_F(RegistrationTest, TheCanvasIsTheReferencesTheUnionOrTheIntersection)
{
    an::Result<an::RegistrationTransform> moved = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Similarity,
                                         .sourceToReference = {1.0, 0.0, 10.0, 0.0, 1.0, -6.0, 0.0, 0.0, 1.0}});
    ASSERT_TRUE(moved.hasValue());
    const std::vector<an::RegistrationTransform> transforms{an::RegistrationTransform{}, *moved};
    const an::Result<an::RegistrationCanvas> reference =
        an::ui::processes::planAlignmentCanvas(transforms, 0u, 100u, 80u, an::RegistrationCanvasMode::Reference);
    ASSERT_TRUE(reference.hasValue()) << reference.status().message();
    EXPECT_EQ(reference->width, 100u);
    EXPECT_EQ(reference->height, 80u);
    const an::Result<an::RegistrationCanvas> united =
        an::ui::processes::planAlignmentCanvas(transforms, 0u, 100u, 80u, an::RegistrationCanvasMode::Union);
    ASSERT_TRUE(united.hasValue()) << united.status().message();
    EXPECT_EQ(united->width, 110u);
    EXPECT_EQ(united->height, 86u);
    const an::Result<an::RegistrationCanvas> common =
        an::ui::processes::planAlignmentCanvas(transforms, 0u, 100u, 80u, an::RegistrationCanvasMode::Intersection);
    ASSERT_TRUE(common.hasValue()) << common.status().message();
    EXPECT_EQ(common->width, 90u);
    EXPECT_EQ(common->height, 74u);
}

TEST_F(RegistrationTest, AnAlignedFrameLandsOnTheReferenceAsFloat32)
{
    const QStringList frames = writeFrames(true);
    an::Result<an::RegistrationTransform> transform =
        an::RegistrationTransform::create(an::RegistrationTransformRequest{
            .linearKind = an::RegistrationTransformKind::Similarity,
            .sourceToReference = {1.0, 0.0, -kShifts[1].first, 0.0, 1.0, -kShifts[1].second, 0.0, 0.0, 1.0}});
    ASSERT_TRUE(transform.hasValue());
    const an::RegistrationCanvas canvas{.width = 320u, .height = 240u};
    const QString aligned = an::ui::processes::alignedOutputPath(frames[1], output());
    ASSERT_TRUE(QDir().mkpath(output()));
    const an::Status status =
        an::ui::processes::alignRegisteredFrame(session(), frames[1], *transform, an::SipDistortion{}, canvas,
                                                an::KernelInterpolation::Linear, {}, aligned, {});
    ASSERT_TRUE(status.ok()) << status.message();
    const std::optional<ReadBack> reference = readBack(session(), frames[0]);
    const std::optional<ReadBack> written = readBack(session(), aligned);
    ASSERT_TRUE(reference.has_value() && written.has_value());
    EXPECT_EQ(written->type, an::SampleType::Float32);
    EXPECT_EQ(written->width, 320u);
    // An integer shift moves every sample the frame reaches onto the reference's.
    double worst = 0.0;
    for (std::uint32_t y = 20u; y < 220u; ++y)
    {
        for (std::uint32_t x = 20u; x < 300u; ++x)
        {
            worst = (std::max)(worst, std::abs(written->at(x, y) - reference->at(x, y)));
        }
    }
    EXPECT_LT(worst, 1e-4);
    // The warp moves the pixels away from the solution the cards stated.
    EXPECT_FALSE(written->card("CRVAL1").has_value());
}

TEST_F(RegistrationTest, AStoppedAlignmentLeavesNoFile)
{
    const QStringList frames = writeFrames();
    std::stop_source stop;
    stop.request_stop();
    const QString aligned = an::ui::processes::alignedOutputPath(frames[1], output());
    ASSERT_TRUE(QDir().mkpath(output()));
    const an::Status status =
        an::ui::processes::alignRegisteredFrame(session(), frames[1], an::RegistrationTransform{}, an::SipDistortion{},
                                                an::RegistrationCanvas{.width = 320u, .height = 240u},
                                                an::KernelInterpolation::Linear, {}, aligned, stop.get_token());
    EXPECT_EQ(status.code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(QFileInfo::exists(aligned));
}

/// The window over the three frames, the first the reference, nothing solved or enriched: its options set before its
/// frames, as a window restoring its settings sets them.
void prepare(RegistrationWindow& window, const QStringList& frames, const QString& output)
{
    auto& ui = window.window->ui();
    ui.referenceModeSelector->setCurrentIndex(ui.referenceModeSelector->findData(QStringLiteral("manual")));
    ui.enrichWithGaiaCatalogCheck->setChecked(false);
    ui.autoSolveUnsolvedFramesCheck->setChecked(false);
    ui.outputDirectory->setText(output);
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->setReferenceRow(0);
}

/// Accepts settings in the integration's registration settings dialog the window's advanced settings open, as a person
/// sets them there, and waits for the window to take them.
void acceptSettings(RegistrationWindow& window, const RegistrationSettings& settings)
{
    window.window->ui().advancedSettingsButton->click();
    RegistrationSettingsDialog* dialog = nullptr;
    ASSERT_TRUE(pumpUntil([&dialog]() { return (dialog = shownDialog<RegistrationSettingsDialog>()) != nullptr; }));
    dialog->setRegistrationSettings(settings);
    dialog->accept();
    ASSERT_TRUE(pumpUntil([&window, &settings]() {
        return window.window->registrationSettings().registrationMethod == settings.registrationMethod;
    }));
}

TEST_F(RegistrationTest, TheWindowRegistersRecordsAndAlignsItsFrames)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    ASSERT_EQ(window.window->referenceRow(), 0);
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const RegisteredFrameSet& set = window.window->registeredFrames();
    ASSERT_EQ(set.frames.size(), 3u);
    EXPECT_EQ(set.referencePath, frames.front());
    EXPECT_TRUE(window.window->registrationIsCurrent());
    EXPECT_TRUE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("registration_metrics.db"))));
    const RegisteredFrame& second = set.frames.at(frames[1]);
    ASSERT_TRUE(second.transform.has_value());
    EXPECT_NEAR(second.transform->translation().x, -kShifts[1].first, 0.05);

    ASSERT_TRUE(window.window->ui().alignButton->isEnabled());
    window.window->alignFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    for (const QString& frame : frames)
    {
        EXPECT_TRUE(QFileInfo::exists(an::ui::processes::alignedOutputPath(frame, output()))) << frame.toStdString();
    }
}

// A frame's cards state a second-order plate distortion, and its stars lie where that distortion puts the reference's
// stars moved by a few pixels. The window matches each frame's stars where its own distortion corrects them to, so the
// transform is the move alone and the record states the distortion; and the alignment undoes it, so every star of the
// aligned frame lies on the reference's.
TEST_F(RegistrationTest, AFramesOwnPlateDistortionIsCorrectedBeforeItIsMatchedAndUndoneWhenItIsAligned)
{
    constexpr double kBend = 1.2e-4;
    constexpr double kDx = 6.0;
    constexpr double kDy = -4.0;
    const StarField field = makeStarField();
    ASSERT_TRUE(field.solution.has_value());
    an::SipPolynomial bendX;
    bendX.order = 2u;
    bendX.coefficients[2][0] = kBend;
    an::SipPolynomial bendY;
    bendY.order = 2u;
    bendY.coefficients[0][2] = kBend;
    const an::Result<an::SipDistortion> distortion = an::SipDistortion::create(an::SipDistortionRequest{
        .referencePixel = field.solution->referencePixel(), .forwardX = bendX, .forwardY = bendY});
    ASSERT_TRUE(distortion.hasValue()) << distortion.status().message();
    an::WcsSolutionRequest bentRequest = field.solution->toRequest();
    bentRequest.distortion = *distortion;
    const an::Result<an::WcsSolution> bent = an::WcsSolution::create(bentRequest);
    ASSERT_TRUE(bent.hasValue()) << bent.status().message();
    // The light's stars where its distortion puts the reference's, moved.
    StarField distorted = field;
    distorted.solution = *bent;
    for (PlantedStar& star : distorted.stars)
    {
        const an::Result<an::SipInverseResult> raw =
            an::applySipInverse(*distortion, an::PixelCoordinate{.x = star.x + kDx, .y = star.y + kDy});
        ASSERT_TRUE(raw.hasValue()) << raw.status().message();
        star.x = raw->pixel.x;
        star.y = raw->pixel.y;
    }
    const QString reference = temporaryPath(QStringLiteral("bent_0.fits"));
    const QString light = temporaryPath(QStringLiteral("bent_1.fits"));
    ASSERT_TRUE(writeShiftedField(session(), reference, field, 0.0, 0.0, false));
    ASSERT_TRUE(writeShiftedField(session(), light, distorted, 0.0, 0.0, true));

    RegistrationWindow window;
    window.window->ui().transformationSelector->setCurrentText(QStringLiteral("Affine"));
    prepare(window, QStringList{reference, light}, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const RegisteredFrameSet& set = window.window->registeredFrames();
    ASSERT_EQ(set.frames.size(), 2u);
    EXPECT_EQ(set.frames.at(reference).distortion.policy, stage_utils::SourceDistortionPolicy::ExplicitNone);
    const RegisteredFrame& moved = set.frames.at(light);
    ASSERT_TRUE(moved.transform.has_value());
    EXPECT_EQ(moved.distortion.policy, stage_utils::SourceDistortionPolicy::ExplicitModel);
    ASSERT_TRUE(moved.distortion.model.has_value());
    EXPECT_NEAR(moved.distortion.model->forwardX().coefficients[2][0], kBend, 1e-12);
    // The corrected stars are the reference's moved, so the transform is the move undone, fitted to a few hundredths.
    EXPECT_NEAR(moved.transform->translation().x, -kDx, 0.05);
    EXPECT_NEAR(moved.transform->translation().y, -kDy, 0.05);
    EXPECT_LT(moved.metrics.astrometric_rms_pixels, 0.1);

    window.window->alignFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    const std::optional<ReadBack> aligned = readBack(session(), an::ui::processes::alignedOutputPath(light, output()));
    ASSERT_TRUE(aligned.has_value());
    for (const PlantedStar& star : field.stars)
    {
        const std::optional<an::PixelCoordinate> centre = centroidNear(*aligned, star.x, star.y, 5, field.background);
        ASSERT_TRUE(centre.has_value()) << star.x << ", " << star.y;
        EXPECT_NEAR(centre->x, star.x, 0.25) << star.x << ", " << star.y;
        EXPECT_NEAR(centre->y, star.y, 0.25) << star.x << ", " << star.y;
    }
}

TEST_F(RegistrationTest, ARegistrationTheDatabaseRefusesIsAFailureAndAlignsNothing)
{
    const QStringList frames = writeFrames();
    // A directory where the database would be: nothing can be recorded there.
    ASSERT_TRUE(QDir().mkpath(QDir(output()).filePath(QStringLiteral("registration_metrics.db"))));
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(context().errorTexts.size(), 1) << context().messages.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(context().errorTexts.front().contains(QStringLiteral("could not be recorded")))
        << context().errorTexts.front().toStdString();
    EXPECT_FALSE(window.window->registrationIsCurrent());
    EXPECT_TRUE(window.window->registeredFrames().frames.empty());
    EXPECT_FALSE(window.window->ui().alignButton->isEnabled());
}

TEST_F(RegistrationTest, AColourRegistrationCorrectedForChromaticAberrationKeepsItsReference)
{
    // Colour frames, each channel the same field, so the correction finds every channel aligned already.
    const StarField field = makeStarField(320u, 240u, 3u);
    QStringList frames;
    for (std::size_t index = 0; index < kShifts.size(); ++index)
    {
        const QString path = temporaryPath(QStringLiteral("colour_%1.fits").arg(index));
        ASSERT_TRUE(writeShiftedField(session(), path, field, kShifts[index].first, kShifts[index].second, false));
        frames << path;
    }
    RegistrationWindow window;
    window.window->ui().chromaticAberrationCorrectionCheck->setChecked(true);
    prepare(window, frames, output());
    ASSERT_TRUE(window.window->registrationSettings().chromaticAberration.enabled);
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(context().errorTexts.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const RegisteredFrameSet& set = window.window->registeredFrames();
    // The frames are matched to the reference the person chose, under the files' own names.
    EXPECT_EQ(set.referencePath, frames.front());
    ASSERT_EQ(set.frames.size(), 3u);
    for (std::size_t index = 0; index < kShifts.size(); ++index)
    {
        const RegisteredFrame& frame = set.frames.at(frames[static_cast<qsizetype>(index)]);
        ASSERT_TRUE(frame.transform.has_value()) << index;
        EXPECT_NEAR(frame.transform->translation().x, -kShifts[index].first, 0.05) << index;
    }
    // The correction is made in memory: nothing is written beside the outputs but the registration database.
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("registration_ca_cache"))));
}

TEST_F(RegistrationTest, ASecondWindowReusesTheRecordedRegistration)
{
    const QStringList frames = writeFrames();
    {
        RegistrationWindow first;
        prepare(first, frames, output());
        first.window->registerFrames();
        ASSERT_TRUE(pumpUntil([&first]() { return first.window->isIdle(); }));
        ASSERT_TRUE(first.window->registrationIsCurrent());
    }
    RegistrationWindow second;
    prepare(second, frames, output());
    // The recorded registration is restored on the thread pool once the list and settings match it.
    ASSERT_TRUE(pumpUntil(
        [&second]() { return second.window->isIdle() && second.window->registeredFrames().frames.size() == 3u; }));
    EXPECT_TRUE(second.window->registrationIsCurrent());
}

TEST_F(RegistrationTest, ExcludedFramesAreLeftOutOfTheAlignment)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->setFramesExcluded({frames[2]}, true);
    EXPECT_EQ(window.window->ignoredFrameIds().size(), 1u);
    window.window->alignFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(QFileInfo::exists(an::ui::processes::alignedOutputPath(frames[1], output())));
    EXPECT_FALSE(QFileInfo::exists(an::ui::processes::alignedOutputPath(frames[2], output())));
}

// Clear All forgets what the window held of the frames it listed - their exclusions and their registration - so none
// of it comes back when the same files are added again. A registration recorded in the output directory is still read
// back when frames are listed, as it is for any list.
TEST_F(RegistrationTest, ClearAllForgetsTheFramesExclusionsAndRegistration)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    ASSERT_EQ(window.window->registeredFrames().frames.size(), 3u);
    window.window->setFramesExcluded({frames[2]}, true);
    ASSERT_EQ(window.window->ignoredFrameIds().size(), 1u);

    window.window->ui().clearAll->click();
    EXPECT_TRUE(window.window->fileList().paths().isEmpty());
    EXPECT_TRUE(window.window->registeredFrames().frames.empty());
    EXPECT_TRUE(window.window->ignoredFrameIds().empty());

    // With no output directory nothing recorded is read back, so what the list shows is what the window kept.
    window.window->ui().outputDirectory->clear();
    window.window->addItemsNonBlocking(frames);
    ASSERT_TRUE(
        pumpUntil([&window]() { return window.window->isIdle() && window.window->fileList().paths().size() == 3; }));
    EXPECT_TRUE(window.window->registeredFrames().frames.empty()) << "the earlier registration came back";
    EXPECT_TRUE(window.window->ignoredFrameIds().empty()) << "the earlier exclusions came back";
}

TEST_F(RegistrationTest, AFrameThatFailedToRegisterIsNotAligned)
{
    QStringList frames = writeFrames();
    const QString sky = temporaryPath(QStringLiteral("sky.fits"));
    ASSERT_TRUE(writeFlatFrame(session(), sky, 320u, 240u, 1u, 0.05, an::SampleType::Float32));
    frames << sky;
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    const RegisteredFrameSet& set = window.window->registeredFrames();
    ASSERT_EQ(set.frames.size(), 4u);
    EXPECT_TRUE(set.frames.at(sky).failed);
    EXPECT_FALSE(set.frames.at(frames[1]).failed);
    window.window->alignFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    EXPECT_TRUE(QFileInfo::exists(an::ui::processes::alignedOutputPath(frames[1], output())));
    EXPECT_FALSE(QFileInfo::exists(an::ui::processes::alignedOutputPath(sky, output())));
}

TEST_F(RegistrationTest, AWindowWithAnotherReferenceLendsItTheStarsARecordedRegistrationFound)
{
    const QStringList frames = writeFrames();
    {
        RegistrationWindow first;
        prepare(first, frames, output());
        first.window->registerFrames();
        ASSERT_TRUE(pumpUntil([&first]() { return first.window->isIdle(); }));
        ASSERT_TRUE(first.window->registrationIsCurrent());
    }
    // The registration recorded no longer answers the reference, but the stars it found still do.
    LogCapture log;
    RegistrationWindow second;
    prepare(second, frames, output());
    second.window->setReferenceRow(1);
    second.window->ui().reprocessAlreadyRegisteredmages->setChecked(true);
    second.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&second]() { return second.window->isIdle(); }));
    EXPECT_EQ(log.count("reusing the recorded registration"), 0u);
    EXPECT_EQ(log.count("reusing the stars of 3 recorded frames"), 1u);
    const RegisteredFrameSet& set = second.window->registeredFrames();
    ASSERT_EQ(set.frames.size(), 3u);
    EXPECT_EQ(set.referencePath, frames[1]);
    for (const auto& [path, frame] : set.frames)
    {
        EXPECT_FALSE(frame.failed) << path.toStdString();
    }
}

TEST_F(RegistrationTest, QualityThresholdsExcludeTheFramesTheyFail)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    window.window->ui().qualityMinStarsValue->setValue(100000);
    window.window->ui().applyThresholdsButton->click();
    // A threshold excludes every frame it fails, the reference among them, as the integration's does.
    EXPECT_EQ(window.window->ignoredFrameIds().size(), 3u);
    window.window->ui().resetThresholdsButton->click();
    EXPECT_TRUE(window.window->ignoredFrameIds().empty());
}

// The integration's blink comparator opens over the frames selected, and the frame the person excludes in it is the one
// the window leaves out once the comparator closes.
TEST_F(RegistrationTest, TheBlinkComparatorsExclusionsAreTheWindows)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    window.window->ui().tableWidget->selectAll();
    ASSERT_TRUE(window.window->ui().blinkCompareButton->isEnabled());
    window.window->ui().blinkCompareButton->click();
    BlinkComparatorDialog* comparator = nullptr;
    ASSERT_TRUE(pumpUntil([&comparator]() { return (comparator = shownDialog<BlinkComparatorDialog>()) != nullptr; }));
    EXPECT_TRUE(window.window->ignoredFrameIds().empty()) << "nothing is left out before the comparator closes";
    comparator->excludeCurrentFrame();
    const QStringList excluded = comparator->comparisonResult().excludedPaths;
    ASSERT_EQ(excluded.size(), 1);
    EXPECT_TRUE(frames.contains(excluded.front()));
    comparator->reject();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->ignoredFrameIds().size() == 1u; }));
    EXPECT_EQ(shownDialog<BlinkComparatorDialog>(), nullptr);
}

// The advanced settings are the integration's registration settings dialog, opened over the window's settings with
// its Auto-Assign answered by the window and without the manual-points method; what the person accepts in it becomes
// the window's settings.
TEST_F(RegistrationTest, TheAdvancedSettingsAreTheIntegrationsDialogAndItsAnswerIsTheWindows)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    RegistrationSettings answer = window.window->registrationSettings();
    answer.maxStarPairs = 777;
    answer.transformMode = QStringLiteral("Polynomial");
    answer.transformOrder = 3;
    window.window->ui().advancedSettingsButton->click();
    RegistrationSettingsDialog* dialog = nullptr;
    ASSERT_TRUE(pumpUntil([&dialog]() { return (dialog = shownDialog<RegistrationSettingsDialog>()) != nullptr; }));
    EXPECT_EQ(dialog->getRegistrationSettings().maxStarPairs, window.window->registrationSettings().maxStarPairs);
    EXPECT_EQ(window.window->registrationSettings().maxStarPairs == 777, false)
        << "the dialog answers only when accepted";
    dialog->setRegistrationSettings(answer);
    dialog->accept();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->registrationSettings().maxStarPairs == 777; }));
    EXPECT_EQ(window.window->registrationSettings().transformOrder, 3);
    EXPECT_EQ(window.window->ui().maxStarParirsValue->value(), 777);
}

// Without a plate solver or a Gaia catalog the window shows its Auto-solve and Gaia boxes off and unavailable, and
// keeps the choices it was given for when one is configured, as the integration's dialog keeps them, rather than store
// what the boxes show.
TEST_F(RegistrationTest, WithoutASolverOrACatalogTheWindowKeepsTheChoicesItCannotUse)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    auto& ui = window.window->ui();
    if (ui.autoSolveUnsolvedFramesCheck->isEnabled() || ui.enrichWithGaiaCatalogCheck->isEnabled())
    {
        GTEST_SKIP() << "a plate solver or a Gaia catalog is configured where the suite runs";
    }
    RegistrationSettings answer = window.window->registrationSettings();
    answer.autoSolveUnsolvedFrames = true;
    answer.enrichWithGaiaCatalog = true;
    answer.maxStarPairs = 777;
    ui.advancedSettingsButton->click();
    RegistrationSettingsDialog* dialog = nullptr;
    ASSERT_TRUE(pumpUntil([&dialog]() { return (dialog = shownDialog<RegistrationSettingsDialog>()) != nullptr; }));
    dialog->setRegistrationSettings(answer);
    dialog->accept();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->registrationSettings().maxStarPairs == 777; }));

    EXPECT_FALSE(ui.autoSolveUnsolvedFramesCheck->isChecked());
    EXPECT_FALSE(ui.enrichWithGaiaCatalogCheck->isChecked());
    const RegistrationSettings kept = window.window->registrationSettings();
    EXPECT_TRUE(kept.autoSolveUnsolvedFrames) << "the window stored the Auto-solve box it shows off";
    EXPECT_TRUE(kept.enrichWithGaiaCatalog) << "the window stored the Gaia box it shows off";
}

// A comet registration runs through the integration's own, which the window reaches through its companion and whose
// refusal it says under its title.
TEST_F(RegistrationTest, ACometRegistrationRunsThroughTheIntegrationsAndItsRefusalIsSaid)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    RegistrationSettings settings = window.window->registrationSettings();
    settings.registrationMethod = QStringLiteral("comet_tracking");
    settings.referenceImage = frames.front();
    RegistrationSettings::CometTrackingSettings comet;
    for (const QString& frame : frames)
    {
        comet.observations.push_back(
            RegistrationSettings::CometTrackingObservation{.frameId = frame, .sourcePosition = QPointF(100.0, 100.0)});
    }
    settings.cometTracking = comet;
    acceptSettings(window, settings);
    ASSERT_EQ(window.window->registrationSettings().registrationMethod, QStringLiteral("comet_tracking"));
    window.window->registerFrames();
    ASSERT_TRUE(pumpUntil([&window]() { return window.window->isIdle(); }));
    // The integration's comet registration reads each frame's capture time to fit the comet's track; frames that state
    // none are refused by name, and the run writes no comet-aligned frame.
    ASSERT_EQ(context().errorTexts.size(), 1);
    EXPECT_TRUE(context().errorTexts.front().contains(QStringLiteral("DATE-OBS")))
        << context().errorTexts.front().toStdString();
    EXPECT_TRUE(context().errorTexts.front().contains(QFileInfo(frames.front()).fileName()))
        << context().errorTexts.front().toStdString();
    QDirIterator written(output(), QDir::Files, QDirIterator::Subdirectories);
    EXPECT_FALSE(written.hasNext()) << "a refused comet registration writes nothing";
}

// A comet registration whose settings track no comet is refused by the window before the integration's runs.
TEST_F(RegistrationTest, ACometRegistrationWithoutObservationsIsRefusedBeforeItRuns)
{
    const QStringList frames = writeFrames();
    RegistrationWindow window;
    prepare(window, frames, output());
    RegistrationSettings settings = window.window->registrationSettings();
    settings.registrationMethod = QStringLiteral("comet_tracking");
    acceptSettings(window, settings);
    ASSERT_EQ(window.window->registrationSettings().registrationMethod, QStringLiteral("comet_tracking"));
    window.window->registerFrames();
    EXPECT_TRUE(window.window->isIdle()) << "the refusal comes before any run starts";
    EXPECT_EQ(context().errorTexts.size(), 1);
    EXPECT_FALSE(QFileInfo::exists(QDir(output()).filePath(QStringLiteral("registration_metrics.db"))))
        << "a refused comet registration records nothing";
}

TEST_F(RegistrationTest, ClosingTheWindowDuringARegistrationWaitsForNothing)
{
    const QStringList frames = writeFrames();
    auto* window = new ImageRegistrationProcess();
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->addItemsNonBlocking(frames);
    ASSERT_TRUE(pumpUntil([window]() { return window->isIdle(); }));
    window->ui().outputDirectory->setText(output());
    window->ui().enrichWithGaiaCatalogCheck->setChecked(false);
    window->registerFrames();
    window->close();
    // What the run would land is dropped on a window that no longer exists.
    for (int round = 0; round < 50; ++round)
    {
        QCoreApplication::processEvents();
    }
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    SUCCEED();
}

TEST_F(RegistrationTest, TheWindowPersistsEveryOptionAndItsFrames)
{
    const QStringList frames = writeFrames();
    std::string stored;
    {
        RegistrationWindow window;
        prepare(window, frames, output());
        auto& ui = window.window->ui();
        ui.registrationMethodSelector->setCurrentIndex(
            ui.registrationMethodSelector->findData(QStringLiteral("phase_correlation")));
        ui.minStarParirsValue->setValue(14);
        ui.maxStarParirsValue->setValue(900);
        ui.transformationSelector->setCurrentText(QStringLiteral("Affine"));
        ui.chromaticAberrationCorrectionCheck->setChecked(true);
        ui.reprocessAlreadyRegisteredmages->setChecked(false);
        ui.alignModeMosaic->setChecked(true);
        ui.interpolationSelector->setCurrentText(QStringLiteral("Bicubic"));
        ui.qualityMaxFwhmValue->setValue(4.5);
        window.window->setReferenceRow(1);
        stored = window.window->serialize_json_settings();
    }
    const nlohmann::json document = nlohmann::json::parse(stored);
    EXPECT_EQ(document.value("version", 0), 4);

    RegistrationWindow restored;
    restored.window->deserialize_json_settings(stored);
    ASSERT_TRUE(pumpUntil([&restored]() { return restored.window->isIdle(); }));
    EXPECT_EQ(restored.window->fileList().paths().size(), 3);
    const RegistrationSettings settings = restored.window->registrationSettings();
    EXPECT_EQ(settings.registrationMethod, QStringLiteral("phase_correlation"));
    EXPECT_EQ(settings.minStarPairs, 14);
    EXPECT_EQ(settings.maxStarPairs, 900);
    EXPECT_EQ(settings.transformation, QStringLiteral("Affine"));
    EXPECT_TRUE(settings.chromaticAberration.enabled);
    EXPECT_EQ(settings.alignmentMode, QStringLiteral("mosaic"));
    EXPECT_EQ(settings.interpolation, QStringLiteral("Bicubic"));
    EXPECT_EQ(restored.window->referenceRow(), 1);
    auto& ui = restored.window->ui();
    EXPECT_EQ(ui.outputDirectory->text(), output());
    EXPECT_FALSE(ui.reprocessAlreadyRegisteredmages->isChecked());
    EXPECT_DOUBLE_EQ(ui.qualityMaxFwhmValue->value(), 4.5);
}

} // namespace
} // namespace an::tests::processes::workflow
