// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the registration keeps beside its images: the stored image positions a person placed - control points and comet
/// observations - stored and read in engine pixel coordinates with their convention stated, and a set that states none
/// not read; the registration hash, which only the canvas a mosaic changes moves among the alignment's settings; and
/// the comet registration's transforms, which carry each frame's nucleus onto the reference's.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "processes/ImageIntegration/core/CometRegistrationProductBridge.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Status.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{

using namespace ImageIntegration::RegistrationHashUtils;

class RegistrationRecordsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
    }

    /// An empty file, so a frame's canonical path names something that exists.
    QString touch(const QString& name)
    {
        const QString path = QDir(m_root.path()).filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        return FrameId(path).canonicalPath();
    }

    /// Settings with a control point and a comet observation, as the registration settings' writer stores them: engine
    /// pixel coordinates, each set stating its convention.
    QJsonObject storedSettings(const QString& frame)
    {
        RegistrationSettings settings;
        settings.registrationMethod = QStringLiteral("manual_points");
        RegistrationSettings::ManualControlPoint point;
        point.sourcePoint = QPointF(10.5, 20.5);
        point.referencePoint = QPointF(11.5, 21.5);
        settings.manualControlPointsByFrame.insert(frame, {point});
        RegistrationSettings::CometTrackingSettings comet;
        RegistrationSettings::CometTrackingObservation observation;
        observation.frameId = frame;
        observation.sourcePosition = QPointF(5.5, 6.5);
        comet.observations.push_back(observation);
        settings.cometTracking = comet;
        return registrationSettingsToJson(settings);
    }

    static void expectEnginePositions(const RegistrationSettings& settings, const QString& frame)
    {
        ASSERT_TRUE(settings.manualControlPointsByFrame.contains(frame));
        const auto& points = settings.manualControlPointsByFrame.value(frame);
        ASSERT_EQ(points.size(), 1);
        EXPECT_EQ(points.front().sourcePoint, QPointF(10.5, 20.5));
        EXPECT_EQ(points.front().referencePoint, QPointF(11.5, 21.5));
        ASSERT_TRUE(settings.cometTracking.has_value());
        ASSERT_EQ(settings.cometTracking->observations.size(), 1);
        EXPECT_EQ(settings.cometTracking->observations.front().sourcePosition, QPointF(5.5, 6.5));
    }

    QTemporaryDir m_root;
};

TEST_F(RegistrationRecordsTest, PositionsAreStoredAndReadInEnginePixelCoordinates)
{
    const QString frame = touch(QStringLiteral("light_002.fits"));
    const QJsonObject stored = storedSettings(frame);
    const QJsonObject entry = stored.value(QStringLiteral("manualControlPoints")).toArray().at(0).toObject();
    EXPECT_EQ(entry.value(QLatin1String(kImagePointPixelConventionKey)).toString(),
              QLatin1String(kImagePointPixelConvention));
    EXPECT_EQ(stored.value(QStringLiteral("cometTracking"))
                  .toObject()
                  .value(QLatin1String(kImagePointPixelConventionKey))
                  .toString(),
              QLatin1String(kImagePointPixelConvention));

    const RegistrationSettings read = registrationSettingsFromJson(stored);
    expectEnginePositions(read, frame);
    expectEnginePositions(registrationSettingsFromJson(registrationSettingsToJson(read)), frame);
}

// A set of positions states the convention it is in. One that states none is no set this version's writer stores, and
// its positions are not read, rather than read half a pixel from where they stand.
TEST_F(RegistrationRecordsTest, ASetOfPositionsThatStatesNoConventionIsNotRead)
{
    const QString frame = touch(QStringLiteral("light_002.fits"));
    QJsonObject stored = storedSettings(frame);
    QJsonArray sets = stored.value(QStringLiteral("manualControlPoints")).toArray();
    QJsonObject set = sets.at(0).toObject();
    set.remove(QLatin1String(kImagePointPixelConventionKey));
    sets.replace(0, set);
    stored.insert(QStringLiteral("manualControlPoints"), sets);
    QJsonObject comet = stored.value(QStringLiteral("cometTracking")).toObject();
    comet.remove(QLatin1String(kImagePointPixelConventionKey));
    stored.insert(QStringLiteral("cometTracking"), comet);

    const RegistrationSettings read = registrationSettingsFromJson(stored);
    EXPECT_FALSE(read.manualControlPointsByFrame.contains(frame));
    ASSERT_TRUE(read.cometTracking.has_value());
    EXPECT_TRUE(read.cometTracking->observations.empty());
}

TEST_F(RegistrationRecordsTest, AManifestCarriesThePositionsThroughItsOwnRecord)
{
    const QString frame = touch(QStringLiteral("light_002.fits"));
    WorkflowManifest manifest;
    WorkflowManifest::RegistrationGroup group;
    group.groupKey = GroupKey{QStringLiteral("reg|positions")};
    group.lightFiles = {FrameId(frame)};
    group.settings = registrationSettingsFromJson(storedSettings(frame));
    manifest.registrationGroups = {group};

    const WorkflowManifest restored = WorkflowManifest::fromJson(manifest.toJson());
    ASSERT_EQ(restored.registrationGroups.size(), 1u);
    expectEnginePositions(restored.registrationGroups.front().settings, frame);
}

TEST_F(RegistrationRecordsTest, OnlyAMosaicCanvasMovesTheRegistrationAmongTheAlignmentSettings)
{
    RegistrationSettings settings;
    const QString standard = registrationTransformHash(settings);

    RegistrationSettings resampled = settings;
    resampled.interpolation = QStringLiteral("Bicubic");
    EXPECT_EQ(registrationTransformHash(resampled), standard);

    RegistrationSettings intersection = settings;
    intersection.alignmentMode = QStringLiteral("intersection");
    EXPECT_EQ(registrationTransformHash(intersection), standard);

    RegistrationSettings mosaic = settings;
    mosaic.alignmentMode = QStringLiteral("mosaic");
    EXPECT_NE(registrationTransformHash(mosaic), standard);

    RegistrationSettings modelled = settings;
    modelled.transformation = QStringLiteral("Affine");
    modelled.transformMode = QStringLiteral("Linear");
    EXPECT_NE(registrationTransformHash(modelled), standard);
}

class CometRegistrationProductsTest : public RegistrationRecordsTest
{
protected:
    void SetUp() override
    {
        RegistrationRecordsTest::SetUp();
        ASSERT_TRUE(m_run.valid());
        for (int index = 0; index < 4; ++index)
        {
            m_frames.push_back(touch(QStringLiteral("comet_%1.fits").arg(index)));
        }
        m_request.groupKey = QStringLiteral("reg|comet");
        m_request.registrationHash = QStringLiteral("registration");
        m_request.lightFilesHash = QStringLiteral("lights");
        m_request.resolvedReferenceFrame = FrameId(m_frames.front());
        m_request.updatedAt = 1000;
        RegistrationSettings::CometTrackingSettings comet;
        for (int index = 0; index < 3; ++index)
        {
            RegistrationSettings::CometTrackingObservation observation;
            observation.frameId = m_frames[static_cast<std::size_t>(index)];
            observation.sourcePosition = nucleus(index);
            comet.observations.push_back(observation);
        }
        m_request.cometSettings = comet;
        for (int index = 0; index < 4; ++index)
        {
            an::ui::CometRegistrationFrameInput frame;
            frame.originalFrameId = FrameId(m_frames[static_cast<std::size_t>(index)]);
            frame.refinementSourcePath = m_frames[static_cast<std::size_t>(index)];
            frame.dateObs = QStringLiteral("2026-03-01T01:%1:00").arg(index * 10, 2, 10, QLatin1Char('0'));
            frame.exposureSeconds = 60.0;
            frame.width = 160;
            frame.height = 120;
            m_request.frames.push_back(frame);
        }
        m_request.refiner =
            [this](const an::ui::CometTrackingFrameContext&, const an::PixelCoordinate&,
                   const an::CometCenterRefinementOptions&) -> an::Result<an::ui::CometRefinementMeasurement> {
            ++m_refinements;
            return an::Status::failure(an::ErrorCode::InvalidData, "the stand-in measures no nucleus");
        };
    }

    /// Where the nucleus is in frame `index`, in engine pixel coordinates: moving uniformly with time.
    static QPointF nucleus(int index)
    {
        return QPointF(50.5 + 4.0 * index, 40.5 + 2.0 * index);
    }

    an::ui::CometRegistrationProductResult build()
    {
        an::Result<an::consumer::OperationScope> scope = m_run.session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        EXPECT_TRUE(scope.hasValue());
        return an::ui::buildCometRegistrationProducts(scope->execution(), m_request);
    }

    an::tests::integration::CalibrationRun m_run;
    std::vector<QString> m_frames;
    an::ui::CometRegistrationProductRequest m_request;
    int m_refinements = 0;
};

TEST_F(CometRegistrationProductsTest, EachFramesNucleusIsCarriedOntoTheReferences)
{
    const an::ui::CometRegistrationProductResult result = build();
    ASSERT_TRUE(result.succeeded) << result.message.toStdString();
    EXPECT_EQ(m_refinements, 1);

    const auto& transforms = result.sidecarRequest.transformProducts.transforms;
    ASSERT_EQ(transforms.size(), 4u);
    for (const auto& role : transforms)
    {
        ASSERT_TRUE(role.succeeded) << role.failureReason.toStdString();
        int index = -1;
        for (std::size_t candidate = 0; candidate < m_frames.size(); ++candidate)
        {
            if (role.frameId == FrameId(m_frames[candidate]))
            {
                index = static_cast<int>(candidate);
            }
        }
        ASSERT_GE(index, 0);
        an::RegistrationTransform transform;
        QString error;
        ASSERT_TRUE(stage_utils::parseRegistrationTransformJson(role.transformJson, transform, error))
            << error.toStdString();
        // The fourth frame was not observed: its nucleus is where the fitted track predicts it.
        const QPointF source = nucleus(index);
        const auto mapped = an::mapRegistrationPoint(transform, {source.x(), source.y()});
        ASSERT_TRUE(mapped.hasValue());
        EXPECT_NEAR(mapped->x, nucleus(0).x(), 1.0e-6) << "frame " << index;
        EXPECT_NEAR(mapped->y, nucleus(0).y(), 1.0e-6) << "frame " << index;
    }
}

TEST_F(CometRegistrationProductsTest, AFrameWithNoExposureTimeFailsItsTransformAndTheRun)
{
    m_request.frames[3].dateObs.clear();
    const an::ui::CometRegistrationProductResult result = build();
    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.tracking.productionFitAvailable);
    EXPECT_EQ(result.sidecarRequest.transformProducts.readiness.readinessStatus, QStringLiteral("Failed"));
    EXPECT_EQ(result.sidecarRequest.points.size(), 4u);

    const FrameId undated(m_frames[3]);
    const auto& transforms = result.sidecarRequest.transformProducts.transforms;
    const auto failed =
        std::find_if(transforms.cbegin(), transforms.cend(),
                     [&](const an::ui::CometTransformRoleRecord& record) { return record.frameId == undated; });
    ASSERT_NE(failed, transforms.cend());
    EXPECT_FALSE(failed->succeeded);
    EXPECT_TRUE(failed->failureReason.contains(QStringLiteral("exposure start")))
        << failed->failureReason.toStdString();
    const auto& points = result.sidecarRequest.points;
    const auto point =
        std::find_if(points.cbegin(), points.cend(),
                     [&](const an::ui::CometTrackingPointRecord& record) { return record.originalFrameId == undated; });
    ASSERT_NE(point, points.cend());
    EXPECT_EQ(point->fitState, QStringLiteral("Failed"));
}

TEST_F(CometRegistrationProductsTest, AStopAskedForEndsTheRegistrationCancelled)
{
    m_request.isCancellationRequested = [] { return true; };
    const an::ui::CometRegistrationProductResult result = build();
    EXPECT_FALSE(result.succeeded);
    EXPECT_TRUE(result.cancelled);
}

TEST_F(CometRegistrationProductsTest, FewerThanTwoObservationsAreRefused)
{
    m_request.cometSettings.observations.resize(1);
    const an::ui::CometRegistrationProductResult result = build();
    EXPECT_FALSE(result.succeeded);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(m_refinements, 0);
}

} // namespace
