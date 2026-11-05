// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <algorithm>

#include <QDir>
#include <QFile>

#include "processes/core/StandaloneRegistrationPersistence.h"
#include "processes/core/StandaloneStackingUtils.h"
#include "processes/core/StandaloneStackingSettingsPersistence.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include "Test_ImageIntegration/TempDirFixture.h"

#include <AfterNightEngine/Algorithms/Normalization/LocalNormalization.h>
#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QDateTime>
#include <QSize>
#include <QTimeZone>

#include <nlohmann/json.hpp>

#include <optional>

namespace
{
/// A grey 3000 x 2000 frame's description, which the stacking list holds beside each file.
an::ImageDescriptor greyFrameDescriptor()
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(3000u, 2000u, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A light as the stacking list holds it: its file, its filter and its exposure, with no capture time, so a session is
/// read from the file.
an::ui::StandaloneStackingFrame makeLightFrame(const QString& path, const QString& filter,
                                               const double exposureSeconds = 300.0)
{
    return an::ui::StandaloneStackingFrame{
        .entry =
            ImageListProcessHelpers::ImageListEntry{.path = path, .descriptor = greyFrameDescriptor(), .metadata = {}},
        .filter = filter,
        .capturedAtUtc = QDateTime(),
        .exposureSeconds = exposureSeconds};
}

/// The same light as the registration records describe it.
FrameInfo makeRegistrationFrame(const QString& path, const QString& filter, const double exposureSeconds = 300.0)
{
    return FrameInfo(FrameId(path), path,
                     FrameMetadata(FrameType::Light, QSize(3000, 2000), exposureSeconds, std::nullopt, 100, 10, filter,
                                   QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                                   QString()));
}

/// A frame registered onto the reference by the identity, with its figures.
an::ui::RegisteredFrame makeRegisteredFrame(const QString& path, const int stars, const double fwhm, const double snr)
{
    an::ui::RegisteredFrame frame;
    frame.sourcePath = path;
    frame.metrics.stars = stars;
    frame.metrics.fwhm = fwhm;
    frame.metrics.snr = snr;
    an::Result<an::RegistrationTransform> identity = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Affine,
                                         .sourceToReference = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(identity.hasValue()) << identity.status().message();
    if (identity.hasValue())
    {
        frame.transform = *identity;
    }
    return frame;
}

void touchFile(const QString& path)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
}
} // namespace

TEST(StandaloneStackingUtils, GroupsByFilterAndBuildsPerGroupOutputNames)
{
    const QString root = QDir::tempPath();
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(root).filePath(QStringLiteral("L_001.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("L_002.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("Ha_001.fit")), QStringLiteral("Ha"), 300.0)};

    an::ui::StandaloneStackingGroupingOptions options;
    options.byFilter = true;

    const auto groups = an::ui::buildStandaloneStackingGroups(images, options, QStringLiteral("stacked_$filter.fit"));
    ASSERT_EQ(groups.size(), 2u);

    const auto findGroup = [&](const QString& filter) -> const an::ui::StandaloneStackingGroup* {
        const auto it = std::find_if(groups.begin(), groups.end(), [&](const an::ui::StandaloneStackingGroup& group) {
            return group.filter == filter;
        });
        return (it != groups.end()) ? &(*it) : nullptr;
    };

    const auto* lGroup = findGroup(QStringLiteral("L"));
    ASSERT_NE(lGroup, nullptr);
    EXPECT_EQ(lGroup->frameCount, 2);
    EXPECT_EQ(lGroup->outputFileName, QStringLiteral("stacked_L.fit"));

    const auto* haGroup = findGroup(QStringLiteral("Ha"));
    ASSERT_NE(haGroup, nullptr);
    EXPECT_EQ(haGroup->frameCount, 1);
    EXPECT_EQ(haGroup->outputFileName, QStringLiteral("stacked_Ha.fit"));
}

TEST(ImageIntegrationRgbCombinationUtils, DetectsPermissiveRgbFiltersInSameRegistrationGroup)
{
    const std::vector<ImageIntegration::RgbIntegrationGroupCandidate> candidates{
        {QStringLiteral("red-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("Deep Red"),
         QStringLiteral("stacked_red.fit"), 12},
        {QStringLiteral("green-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("GREEN channel"),
         QStringLiteral("stacked_green.fit"), 13},
        {QStringLiteral("blue-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("blue"),
         QStringLiteral("stacked_blue.fit"), 11},
        {QStringLiteral("lum-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("Luminance"),
         QStringLiteral("stacked_l.fit"), 16},
    };

    const auto groups = ImageIntegration::findEligibleRgbIntegrationGroups(candidates);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.front().registrationGroupKey, QStringLiteral("reg|3000x2000|M42"));
    EXPECT_EQ(groups.front().red.groupKey, QStringLiteral("red-key"));
    EXPECT_EQ(groups.front().green.groupKey, QStringLiteral("green-key"));
    EXPECT_EQ(groups.front().blue.groupKey, QStringLiteral("blue-key"));
}

TEST(ImageIntegrationRgbCombinationUtils, RejectsRgbFiltersSplitAcrossRegistrationGroups)
{
    const std::vector<ImageIntegration::RgbIntegrationGroupCandidate> candidates{
        {QStringLiteral("red-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("Red"),
         QStringLiteral("stacked_red.fit"), 12},
        {QStringLiteral("green-key"), QStringLiteral("reg|3000x2000|M31"), QStringLiteral("Green"),
         QStringLiteral("stacked_green.fit"), 13},
        {QStringLiteral("blue-key"), QStringLiteral("reg|3000x2000|M42"), QStringLiteral("Blue"),
         QStringLiteral("stacked_blue.fit"), 11},
    };

    EXPECT_TRUE(ImageIntegration::findEligibleRgbIntegrationGroups(candidates).empty());
    EXPECT_FALSE(ImageIntegration::hasEligibleRgbIntegrationGroup(candidates));
}

TEST(StandaloneStackingUtils, GroupsByExposureUsingToleranceFraction)
{
    const QString root = QDir::tempPath();
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(root).filePath(QStringLiteral("exp_300_a.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("exp_309_b.fit")), QStringLiteral("L"), 309.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("exp_420_c.fit")), QStringLiteral("L"), 420.0)};

    an::ui::StandaloneStackingGroupingOptions options;
    options.byFilter = false;
    options.byExposure = true;
    options.exposureToleranceFraction = 0.05;

    const auto groups = an::ui::buildStandaloneStackingGroups(images, options, QStringLiteral("stacked_$exposure.fit"));
    ASSERT_EQ(groups.size(), 2u);

    EXPECT_EQ(groups[0].frameCount, 2);
    EXPECT_EQ(groups[1].frameCount, 1);
    EXPECT_NE(groups[0].outputFileName, groups[1].outputFileName);
}

TEST(StandaloneStackingUtils, DuplicateResolvedOutputNamesAreDisambiguatedPerGroup)
{
    const QString root = QDir::tempPath();
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(root).filePath(QStringLiteral("2024-01-01/L_001.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("2024-01-02/L_002.fit")), QStringLiteral("L"), 300.0)};

    an::ui::StandaloneStackingGroupingOptions options;
    options.byFilter = true;
    options.bySession = true;

    const auto groups = an::ui::buildStandaloneStackingGroups(images, options, QStringLiteral("stacked_$filter.fit"));
    ASSERT_EQ(groups.size(), 2u);

    EXPECT_EQ(groups[0].outputFileName, QStringLiteral("stacked_L.fit"));
    EXPECT_NE(groups[1].outputFileName, groups[0].outputFileName);
    EXPECT_TRUE(groups[1].outputFileName.startsWith(QStringLiteral("stacked_L_")));
    EXPECT_TRUE(groups[1].outputFileName.endsWith(QStringLiteral(".fit")));
}

TEST_F(TempDirFixture, WeightingFallsBackWhenMetricsDatabaseIsAbsent)
{
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("one.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("two.fit")), QStringLiteral("L"), 300.0)};

    const auto result =
        an::ui::planStandaloneStackingWeights(QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")),
                                              images, an::ui::StandaloneStackingWeightingMode::Fwhm);

    EXPECT_TRUE(result.usedFallback);
    EXPECT_FALSE(result.warning.isEmpty());
    EXPECT_EQ(result.method, an::StackWeighting::Uniform);
    EXPECT_TRUE(result.measurements.empty());
}

TEST(StandaloneStackingUtils, NoGroupingOptionsProduceSingleAllFramesGroup)
{
    const QString root = QDir::tempPath();
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(root).filePath(QStringLiteral("L_001.fit")), QStringLiteral("L"), 300.0),
        makeLightFrame(QDir(root).filePath(QStringLiteral("Ha_001.fit")), QStringLiteral("Ha"), 600.0)};

    an::ui::StandaloneStackingGroupingOptions options;
    options.byFilter = false;
    options.bySession = false;
    options.byExposure = false;

    const auto groups = an::ui::buildStandaloneStackingGroups(images, options, QStringLiteral("stacked.fit"));
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].frameCount, 2);
    EXPECT_EQ(groups[0].label, QStringLiteral("All Frames"));
    EXPECT_EQ(groups[0].outputFileName, QStringLiteral("stacked.fit"));
}

TEST(StandaloneStackingUtils, BroadbandFilterProducesGroupFallbackInOutputName)
{
    // "Broadband" is cleared to empty, so $filter expands via safeTemplateValue("") which
    // calls sanitizeFilename(""), and that function returns the sentinel "group" to avoid an
    // empty filename component.  The resulting output name is "stacked_group.fit".
    an::ui::StandaloneStackingGroup group;
    group.filter = QStringLiteral("Broadband");
    group.session = QStringLiteral("2024-01-01");
    group.representativeExposureSeconds = 300.0;
    group.label = QStringLiteral("Broadband");

    const QString result = an::ui::resolveStandaloneStackingOutputName(QStringLiteral("stacked_$filter.fit"), group);
    EXPECT_EQ(result, QStringLiteral("stacked_group.fit"));
}

TEST(StandaloneStackingUtils, OutputTemplateWithNoExtensionDefaultsToFitExtension)
{
    an::ui::StandaloneStackingGroup group;
    group.filter = QStringLiteral("L");
    group.session = QStringLiteral("2024-01-01");
    group.representativeExposureSeconds = 300.0;
    group.label = QStringLiteral("L");

    const QString result = an::ui::resolveStandaloneStackingOutputName(QStringLiteral("stacked_$filter"), group);
    EXPECT_EQ(result, QStringLiteral("stacked_L.fit"));
}

TEST(StandaloneStackingUtils, OutputTemplateWithUnsupportedExtensionDefaultsToFitExtension)
{
    an::ui::StandaloneStackingGroup group;
    group.filter = QStringLiteral("Red");
    group.session = QStringLiteral("2025-04-29");
    group.representativeExposureSeconds = 90.0;
    group.label = QStringLiteral("Red");

    const QString result =
        an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.fitsa"), group);
    EXPECT_EQ(result, QStringLiteral("master_flat_Red.fit"));
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.raw"), group),
              QStringLiteral("master_flat_Red.fit"));
    EXPECT_EQ(an::ui::resolveStandaloneStackingOutputName(QStringLiteral("master_flat_$filter.xisf"), group),
              QStringLiteral("master_flat_Red.xisf"));
}

TEST(StandaloneStackingUtils, WeightingRequiresMetricsTrueOnlyForMetricModes)
{
    using Mode = an::ui::StandaloneStackingWeightingMode;
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Equal));
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Noise));
    EXPECT_FALSE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Exposure));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Fwhm));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Snr));
    EXPECT_TRUE(an::ui::standaloneStackingWeightingRequiresMetrics(Mode::Stars));
}

TEST(StandaloneStackingUtils, TransformRoleGuardAllowsUnqualifiedOrdinaryStacking)
{
    an::ui::StandaloneStackingTransformRoleRequest request;
    request.stackingMode = QStringLiteral("Stars");

    const auto result = an::ui::validateStandaloneStackingTransformRoleSelection(request);

    EXPECT_TRUE(result.allowed) << result.error.toStdString();
    EXPECT_EQ(result.effectiveRole, QStringLiteral("ordinary"));
    EXPECT_TRUE(result.error.isEmpty());
}

TEST_F(TempDirFixture, TransformRoleGuardRejectsCometRoleWhenExactCometRunIsMissing)
{
    const QString pathA = QDir(tempPath()).filePath(QStringLiteral("role_guard_a.fit"));
    const QString pathB = QDir(tempPath()).filePath(QStringLiteral("role_guard_b.fit"));
    touchFile(pathA);
    touchFile(pathB);

    const std::vector<FrameInfo> images{makeRegistrationFrame(pathA, QStringLiteral("L"), 300.0),
                                        makeRegistrationFrame(pathB, QStringLiteral("L"), 300.0)};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(pathA).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    an::ui::RegisteredFrameSet registeredData;
    registeredData.referencePath = pathA;
    registeredData.frames.emplace(pathA, makeRegisteredFrame(pathA, 18, 2.7, 16.0));
    registeredData.frames.emplace(pathB, makeRegisteredFrame(pathB, 21, 2.5, 18.0));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    an::ui::StandaloneStackingTransformRoleRequest request;
    request.registrationMetricsDbPath = context->databasePath;
    request.stackingMode = QStringLiteral("Comet");
    request.requestedRole = QStringLiteral("comet_aligned");
    request.frameIds = {FrameId(pathA), FrameId(pathB)};

    const auto result = an::ui::validateStandaloneStackingTransformRoleSelection(request);

    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(result.effectiveRole, QStringLiteral("comet_aligned"));
    EXPECT_TRUE(result.error.contains(QStringLiteral("Standalone comet stacking"), Qt::CaseInsensitive))
        << result.error.toStdString();
    EXPECT_TRUE(result.error.contains(QStringLiteral("comet_aligned"), Qt::CaseInsensitive))
        << result.error.toStdString();
}

TEST_F(TempDirFixture, TransformRoleGuardResolvesActiveCometRegisteredOutputs)
{
    const QString pathA = QDir(tempPath()).filePath(QStringLiteral("source_a.fit"));
    const QString pathB = QDir(tempPath()).filePath(QStringLiteral("source_b.fit"));
    const QString cometA = QDir(tempPath()).filePath(QStringLiteral("comet_aligned_a.fit"));
    const QString cometB = QDir(tempPath()).filePath(QStringLiteral("comet_aligned_b.fit"));
    touchFile(pathA);
    touchFile(pathB);
    touchFile(cometA);
    touchFile(cometB);

    const std::vector<FrameInfo> images{makeRegistrationFrame(pathA, QStringLiteral("L"), 300.0),
                                        makeRegistrationFrame(pathB, QStringLiteral("L"), 300.0)};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(pathA).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    an::ui::RegisteredFrameSet registeredData;
    registeredData.referencePath = pathA;
    registeredData.frames.emplace(pathA, makeRegisteredFrame(pathA, 18, 2.7, 16.0));
    registeredData.frames.emplace(pathB, makeRegisteredFrame(pathB, 21, 2.5, 18.0));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    an::ui::RegistrationMetricsWriter writer(context->databasePath, QStringLiteral("StandaloneCometSuccessTest"));
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::CometTrackingRunRecord run;
    run.cometRunId = QStringLiteral("standalone-comet-run");
    run.groupKey = context->groupKey;
    run.settingsVersion = context->settingsVersion;
    run.cometContextId = QStringLiteral("standalone-comet-context");
    run.detectionHash = context->detectionHash;
    run.registrationHash = context->registrationHash;
    run.lightFilesHash = context->lightFilesHash;
    run.calibrationHash = context->calibrationHash;
    run.resolvedReferenceFrameId = context->referenceFrameId;
    run.mode = QStringLiteral("PureComet");
    run.status = QStringLiteral("RegistrationReady");
    ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

    an::ui::CometOutputPlaneRecord plane;
    plane.cometOutputPlaneId = QStringLiteral("standalone-comet-plane");
    plane.cometRunId = run.cometRunId;
    plane.role = QStringLiteral("comet_aligned");
    plane.alignmentMode = QStringLiteral("PureComet");
    plane.coordinateFrameKind = QStringLiteral("CometReference");
    plane.canvasWidth = 64;
    plane.canvasHeight = 64;
    plane.skyWcsValid = false;
    plane.skyWcsInvalidReason = QStringLiteral("non-sidereal");
    ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();

    an::ui::CometRoleReadinessRecord readiness;
    readiness.cometRunId = run.cometRunId;
    readiness.role = QStringLiteral("comet_aligned");
    readiness.readinessStatus = QStringLiteral("Ready");
    readiness.selectedFrameCount = 2;
    readiness.successfulTransformCount = 2;
    readiness.outputPlaneStatus = QStringLiteral("Ready");
    readiness.requiresMaterializedOutputs = true;
    readiness.materializedOutputCount = 2;
    ASSERT_TRUE(writer.upsertCometRoleReadiness(readiness, &error)) << error.toStdString();

    an::ui::CometRegisteredOutputRecord outputA;
    outputA.cometRunId = run.cometRunId;
    outputA.frameId = FrameId(pathA);
    outputA.role = QStringLiteral("comet_aligned");
    outputA.cometOutputPlaneId = plane.cometOutputPlaneId;
    outputA.outputPath = cometA;
    outputA.artifactHash = QStringLiteral("hash-a");
    outputA.succeeded = true;
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(outputA, &error)) << error.toStdString();

    an::ui::CometRegisteredOutputRecord outputB = outputA;
    outputB.frameId = FrameId(pathB);
    outputB.outputPath = cometB;
    outputB.artifactHash = QStringLiteral("hash-b");
    ASSERT_TRUE(writer.upsertCometRegisteredOutput(outputB, &error)) << error.toStdString();

    ASSERT_TRUE(writer.activateCometTrackingRun(run.cometRunId, {QStringLiteral("comet_aligned")}, true, &error))
        << error.toStdString();

    an::ui::StandaloneStackingTransformRoleRequest request;
    request.registrationMetricsDbPath = context->databasePath;
    request.stackingMode = QStringLiteral("Comet");
    request.frameIds = {FrameId(pathA), FrameId(pathB)};

    const auto result = an::ui::validateStandaloneStackingTransformRoleSelection(request);

    ASSERT_TRUE(result.allowed) << result.error.toStdString();
    EXPECT_EQ(result.effectiveRole, QStringLiteral("comet_aligned"));
    ASSERT_EQ(result.resolvedInputPaths.size(), 2u);
    EXPECT_EQ(result.resolvedInputPaths[0], cometA);
    EXPECT_EQ(result.resolvedInputPaths[1], cometB);
}

TEST_F(TempDirFixture, ExposureWeightingFallsBackWhenAllExposuresAreZero)
{
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("a.fit")), QStringLiteral("L"), 0.0),
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("b.fit")), QStringLiteral("L"), 0.0)};

    const auto result =
        an::ui::planStandaloneStackingWeights(QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")),
                                              images, an::ui::StandaloneStackingWeightingMode::Exposure);

    EXPECT_TRUE(result.usedFallback);
    EXPECT_FALSE(result.warning.isEmpty());
    EXPECT_EQ(result.method, an::StackWeighting::Uniform);
    EXPECT_TRUE(result.measurements.empty());
}

TEST_F(TempDirFixture, ExposureWeightingNormalizesWeightsWhenExposuresVary)
{
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("short.fit")), QStringLiteral("L"), 100.0),
        makeLightFrame(QDir(tempPath()).filePath(QStringLiteral("long.fit")), QStringLiteral("L"), 300.0)};

    const auto result =
        an::ui::planStandaloneStackingWeights(QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db")),
                                              images, an::ui::StandaloneStackingWeightingMode::Exposure);

    // The stack weighs each frame by the exposure it is handed, so the longer exposure weighs more; the engine
    // normalizes the weights.
    EXPECT_FALSE(result.usedFallback);
    EXPECT_EQ(result.method, an::StackWeighting::Exposure);
    ASSERT_EQ(result.measurements.size(), 2u);
    ASSERT_TRUE(result.measurements[0].exposureSeconds.has_value());
    ASSERT_TRUE(result.measurements[1].exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*result.measurements[0].exposureSeconds, 100.0);
    EXPECT_DOUBLE_EQ(*result.measurements[1].exposureSeconds, 300.0);
    EXPECT_GT(*result.measurements[1].exposureSeconds, *result.measurements[0].exposureSeconds);
}

TEST_F(TempDirFixture, MetricWeightingWarnsWhenSomeFramesLackUsableMetrics)
{
    const QString registeredPath = QDir(tempPath()).filePath(QStringLiteral("registered.fit"));
    const QString missingPath = QDir(tempPath()).filePath(QStringLiteral("missing.fit"));
    touchFile(registeredPath);
    touchFile(missingPath);

    const std::vector<FrameInfo> registrationFrames{makeRegistrationFrame(registeredPath, QStringLiteral("L"), 300.0),
                                                    makeRegistrationFrame(missingPath, QStringLiteral("L"), 300.0)};
    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(registeredPath, QStringLiteral("L"), 300.0),
        makeLightFrame(missingPath, QStringLiteral("L"), 300.0)};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(registeredPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context =
        an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), registrationFrames, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    an::ui::RegisteredFrameSet registeredData;
    registeredData.referencePath = registeredPath;
    an::ui::RegisteredFrame registeredImage = makeRegisteredFrame(registeredPath, 24, 2.4, 18.0);
    registeredImage.metrics.eccentricity = 0.08;
    registeredData.frames.emplace(registeredPath, registeredImage);

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, registrationFrames, registeredData, &error))
        << error.toStdString();

    const auto result = an::ui::planStandaloneStackingWeights(context->databasePath, images,
                                                              an::ui::StandaloneStackingWeightingMode::Fwhm);

    // The measured frame carries its width; the other carries none, which the warning names, and the set is weighed
    // as partly measured rather than falling back.
    EXPECT_FALSE(result.usedFallback);
    EXPECT_TRUE(result.partlyMeasured);
    EXPECT_FALSE(result.warning.isEmpty());
    EXPECT_NE(result.warning.indexOf(QStringLiteral("missing.fit")), -1) << result.warning.toStdString();
    ASSERT_EQ(result.measurements.size(), 2u);
    ASSERT_TRUE(result.measurements[0].fullWidth.has_value());
    EXPECT_DOUBLE_EQ(*result.measurements[0].fullWidth, 2.4);
    EXPECT_FALSE(result.measurements[1].fullWidth.has_value());
}

TEST_F(TempDirFixture, MetricWeightingMatchesNewestWindowsAliasFrameIdentity)
{
    const QString databasePath = QDir(tempPath()).filePath(QStringLiteral("registration_metrics.db"));
    const FrameId currentFrame(QStringLiteral("C:/Current/Standalone/Light.fit"));
    const FrameId aliasFrame(QStringLiteral("c:\\current\\standalone\\light.fit"));
    an::ui::RegistrationMetricsWriter writer(databasePath, QStringLiteral("StandaloneWeightWriter"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

    an::ui::RegistrationMetricRecord metric;
    metric.frameId = aliasFrame;
    metric.groupKey = QStringLiteral("reg|current|standalone-weight");
    metric.settingsVersion = 1;
    metric.metrics.succeeded = true;
    metric.metrics.fwhm = 2.0;
    metric.updatedAt = 200;
    ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

    const std::vector<an::ui::StandaloneStackingFrame> images{
        makeLightFrame(currentFrame.canonicalPath(), QStringLiteral("L"), 300.0)};
    const auto result =
        an::ui::planStandaloneStackingWeights(databasePath, images, an::ui::StandaloneStackingWeightingMode::Fwhm);

    EXPECT_FALSE(result.usedFallback) << result.warning.toStdString();
    ASSERT_EQ(result.measurements.size(), 1u);
    ASSERT_TRUE(result.measurements.front().fullWidth.has_value());
    EXPECT_DOUBLE_EQ(*result.measurements.front().fullWidth, 2.0);
}

TEST(StandaloneStackingUtils, LocalNormalizationSettingsProduceEnabledEngineParams)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.gridSize = 8;
    settings.localNormalization.gridInterpolation = QStringLiteral("Bilinear");
    settings.localNormalization.localScaleEnabled = true;
    settings.localNormalization.referenceStrategy = QStringLiteral("Single Best Frame");
    settings.localNormalization.backgroundEstimator = QStringLiteral("Percentile");
    settings.localNormalization.backgroundPercentile = 0.30;
    settings.scaleEstimator = ScaleEstimatorMethod::MAD;

    // The local pass is the engine's local-normalization request over the group's settings; the reference strategy
    // and the local scale are the normalization stage's own and reach no field of it.
    const an::LocalNormalizationRequest request =
        ImageIntegration::localNormalizationRequestFor(settings.localNormalization);

    EXPECT_EQ(request.gridSize, 8u);
    EXPECT_EQ(request.interpolation, an::LocalGridInterpolation::Bilinear);
    EXPECT_EQ(request.estimator, an::LocalBackgroundEstimator::Percentile);
    EXPECT_NEAR(request.backgroundPercentile, 0.30, 1e-9);
    EXPECT_EQ(ImageIntegration::scaleEstimatorFor(settings.scaleEstimator),
              an::ScaleEstimator::MedianAbsoluteDeviation);
}

TEST(StandaloneStackingUtils, LocalNormalizationGridSizeIsClampedForEngineParams)
{
    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.gridSize = 1000000;

    const an::LocalNormalizationRequest request =
        ImageIntegration::localNormalizationRequestFor(settings.localNormalization);

    EXPECT_LE(request.gridSize, an::kMaxLocalNormalizationGridSize);
    EXPECT_EQ(request.gridSize, 256u);
}

TEST(StandaloneStackingUtils, SynchronizeSharedLocalNormalizationReplacesStaleLocalOverrides)
{
    LocalNormalizationSettings sharedSettings;
    sharedSettings.gridSize = 8;
    sharedSettings.gridInterpolation = QStringLiteral("Bilinear");
    sharedSettings.localScaleEnabled = true;
    sharedSettings.referenceStrategy = QStringLiteral("Single Best Frame");
    sharedSettings.backgroundEstimator = QStringLiteral("Percentile");
    sharedSettings.backgroundPercentile = 0.3;
    sharedSettings.maxIntegratedFrames = 12;

    StackingSettings settings;
    settings.normalizationMode = NormalizationMode::Local;
    settings.localNormalization.enabled = true;
    settings.localNormalization.gridSize = 2;
    settings.localNormalization.gridInterpolation = QStringLiteral("Bicubic");
    settings.localNormalization.localScaleEnabled = false;
    settings.localNormalization.referenceStrategy = QStringLiteral("Integrate Best Frames");
    settings.localNormalization.backgroundEstimator = QStringLiteral("Sigma Clipped Median");
    settings.localNormalization.backgroundPercentile = 0.1;
    settings.localNormalization.maxIntegratedFrames = 3;

    an::ui::synchronizeStandaloneStackingLocalNormalization(settings, sharedSettings);

    EXPECT_TRUE(settings.localNormalization.enabled);
    EXPECT_EQ(settings.localNormalization.gridSize, sharedSettings.gridSize);
    EXPECT_EQ(settings.localNormalization.gridInterpolation, sharedSettings.gridInterpolation);
    EXPECT_EQ(settings.localNormalization.localScaleEnabled, sharedSettings.localScaleEnabled);
    EXPECT_EQ(settings.localNormalization.referenceStrategy, sharedSettings.referenceStrategy);
    EXPECT_EQ(settings.localNormalization.backgroundEstimator, sharedSettings.backgroundEstimator);
    EXPECT_DOUBLE_EQ(settings.localNormalization.backgroundPercentile, sharedSettings.backgroundPercentile);
    EXPECT_EQ(settings.localNormalization.maxIntegratedFrames, sharedSettings.maxIntegratedFrames);
}

TEST(StandaloneStackingUtils, ResetStandaloneLocalNormalizationRestoresDefaults)
{
    LocalNormalizationSettings settings;
    settings.gridSize = 8;
    settings.gridInterpolation = QStringLiteral("Bilinear");
    settings.localScaleEnabled = true;
    settings.referenceStrategy = QStringLiteral("Single Best Frame");
    settings.maxIntegratedFrames = 32;
    settings.backgroundEstimator = QStringLiteral("Percentile");
    settings.backgroundPercentile = 0.3;

    an::ui::resetStandaloneStackingLocalNormalizationSettings(settings);

    const LocalNormalizationSettings defaults = an::ui::standaloneStackingDefaultLocalNormalizationSettings();
    EXPECT_EQ(settings.gridSize, defaults.gridSize);
    EXPECT_EQ(settings.gridInterpolation, defaults.gridInterpolation);
    EXPECT_EQ(settings.localScaleEnabled, defaults.localScaleEnabled);
    EXPECT_EQ(settings.referenceStrategy, defaults.referenceStrategy);
    EXPECT_EQ(settings.maxIntegratedFrames, defaults.maxIntegratedFrames);
    EXPECT_EQ(settings.backgroundEstimator, defaults.backgroundEstimator);
    EXPECT_DOUBLE_EQ(settings.backgroundPercentile, defaults.backgroundPercentile);
}

TEST(StandaloneStackingUtils, LocalNormalizationPersistenceRoundTripsCustomValues)
{
    LocalNormalizationSettings input;
    input.gridSize = 8;
    input.gridInterpolation = QStringLiteral("Bilinear");
    input.localScaleEnabled = true;
    input.referenceStrategy = QStringLiteral("Single Best Frame");
    input.maxIntegratedFrames = 32;
    input.evaluationCriteria = QStringLiteral("Median");
    input.backgroundEstimator = QStringLiteral("Percentile");
    input.backgroundPercentile = 0.3;
    input.localScaleSmoothingRadius = 4;

    nlohmann::json params;
    an::ui::serializeStandaloneStackingLocalNormalization(params, input);
    const LocalNormalizationSettings output = an::ui::deserializeStandaloneStackingLocalNormalization(params);

    EXPECT_EQ(output.gridSize, input.gridSize);
    EXPECT_EQ(output.gridInterpolation, input.gridInterpolation);
    EXPECT_EQ(output.localScaleEnabled, input.localScaleEnabled);
    EXPECT_EQ(output.backgroundEstimator, input.backgroundEstimator);
    EXPECT_DOUBLE_EQ(output.backgroundPercentile, input.backgroundPercentile);
    EXPECT_EQ(output.localScaleSmoothingRadius, input.localScaleSmoothingRadius);
    // The stack fits each frame against its reference frame, so what only the integration's analysis reads is not kept.
    const LocalNormalizationSettings defaults = an::ui::standaloneStackingDefaultLocalNormalizationSettings();
    EXPECT_EQ(output.referenceStrategy, defaults.referenceStrategy);
    EXPECT_EQ(output.maxIntegratedFrames, defaults.maxIntegratedFrames);
    EXPECT_EQ(output.evaluationCriteria, defaults.evaluationCriteria);
}

TEST(StandaloneStackingUtils, LocalNormalizationPersistenceDefaultsWhenItsBlockIsMissing)
{
    nlohmann::json paramsWithoutBlock;
    paramsWithoutBlock["normalizationMode"] = "Local";
    paramsWithoutBlock["stackMethod"] = "Average";

    const LocalNormalizationSettings output =
        an::ui::deserializeStandaloneStackingLocalNormalization(paramsWithoutBlock);
    const LocalNormalizationSettings defaults = an::ui::standaloneStackingDefaultLocalNormalizationSettings();

    EXPECT_EQ(output.gridSize, defaults.gridSize);
    EXPECT_EQ(output.gridInterpolation, defaults.gridInterpolation);
    EXPECT_EQ(output.referenceStrategy, defaults.referenceStrategy);
    EXPECT_EQ(output.backgroundEstimator, defaults.backgroundEstimator);
    EXPECT_DOUBLE_EQ(output.backgroundPercentile, defaults.backgroundPercentile);
    EXPECT_EQ(output.localScaleSmoothingRadius, defaults.localScaleSmoothingRadius);
}
