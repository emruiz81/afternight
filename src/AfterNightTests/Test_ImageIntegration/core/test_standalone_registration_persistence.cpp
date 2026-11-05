// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QThread>

#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/core/StandaloneRegistrationPersistence.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include "Test_ImageIntegration/TempDirFixture.h"

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <QDateTime>
#include <QSize>
#include <QTimeZone>

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace
{
using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;
using an::ui::RegisteredStar;

/// The stars a registration recorded for a frame, at their engine pixel coordinates.
std::vector<RegisteredStar> makeStars(std::initializer_list<QPointF> positions)
{
    std::vector<RegisteredStar> stars;
    stars.reserve(positions.size());
    for (const auto& position : positions)
    {
        stars.push_back(RegisteredStar{.x = position.x(), .y = position.y(), .fwhm = 2.0, .roundness = 0.9});
    }
    return stars;
}

/// A translation onto the reference, as a projective transform.
an::RegistrationTransform makeTranslationTransform(double dx, double dy)
{
    an::Result<an::RegistrationTransform> transform = an::RegistrationTransform::create(
        an::RegistrationTransformRequest{.linearKind = an::RegistrationTransformKind::Projective,
                                         .sourceToReference = {1.0, 0.0, dx, 0.0, 1.0, dy, 0.0, 0.0, 1.0}});
    EXPECT_TRUE(transform.hasValue()) << transform.status().message();
    return *transform;
}

/// A frame registered from a file: its stars, its figures and, unless it failed, its transform.
RegisteredFrame makeRegistered(const QString& path, std::vector<RegisteredStar> stars,
                               std::optional<an::RegistrationTransform> transform, const bool failed = false)
{
    RegisteredFrame frame;
    frame.sourcePath = path;
    frame.stars = std::move(stars);
    frame.metrics.stars = static_cast<int>(frame.stars.size());
    frame.failed = failed;
    if (!failed)
    {
        frame.transform = std::move(transform);
    }
    return frame;
}

/// A light as the integration's import describes it: its size, its target and the acquisition the group key reads.
FrameInfo makeLightFrame(const QString& path, const int width, const int height, const QString& objectName)
{
    return FrameInfo(FrameId(path), path,
                     FrameMetadata(FrameType::Light, QSize(width, height), 60.0, std::nullopt, 100, 10,
                                   QStringLiteral("L"), objectName,
                                   QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString()));
}
} // namespace

TEST_F(TempDirFixture, StandaloneRegistrationPersistenceRoundTripsCurrentSessionState)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    {
        QFile frameAFile(frameAPath);
        ASSERT_TRUE(frameAFile.open(QIODevice::WriteOnly));
    }
    {
        QFile frameBFile(frameBPath);
        ASSERT_TRUE(frameBFile.open(QIODevice::WriteOnly));
    }
    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("M42"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("M42"));
    const std::vector<FrameInfo> images{frameA, frameB};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(frameAPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");
    settings.debayerMethod = QStringLiteral("RCD");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();
    EXPECT_TRUE(context->interoperableGroup);
    EXPECT_EQ(context->groupKey, QStringLiteral("reg|3000x2000|M42"));

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;

    RegisteredFrame imageA = makeRegistered(frameAPath, makeStars({QPointF(10.0, 10.0), QPointF(20.0, 30.0)}),
                                            makeTranslationTransform(0.0, 0.0));
    imageA.metrics.fwhm = 2.1;
    imageA.metrics.eccentricity = 0.05;
    imageA.metrics.snr = 30.0;
    imageA.quality = 12.0;
    registeredData.frames.emplace(frameAPath, imageA);

    RegisteredFrame imageB =
        makeRegistered(frameBPath, makeStars({QPointF(15.0, 16.0), QPointF(22.0, 11.0), QPointF(18.0, 40.0)}),
                       makeTranslationTransform(4.0, -3.5));
    imageB.metrics.fwhm = 2.9;
    imageB.metrics.eccentricity = 0.12;
    imageB.metrics.snr = 18.5;
    imageB.quality = 8.0;
    imageB.noise.background_noise = 0.0021;
    imageB.noise.global_mad = 0.0031;
    imageB.noise.global_scale = 0.0041;
    registeredData.frames.emplace(frameBPath, imageB);

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    const auto loaded = an::ui::loadStandaloneRegistrationState(*context, images, &error);
    ASSERT_TRUE(loaded.has_value()) << error.toStdString();
    ASSERT_TRUE(loaded->groupState.has_value());
    EXPECT_EQ(loaded->registeredData.frames.size(), 2u);
    EXPECT_EQ(loaded->groupState->referenceFrameId, context->referenceFrameId);
    EXPECT_EQ(loaded->context.groupKey, context->groupKey);
    ASSERT_EQ(loaded->frameMetrics.size(), 2u);
    ASSERT_EQ(loaded->metricsByFrameId.size(), 2u);
    ASSERT_EQ(loaded->registeredImagesByFrameId.size(), 2u);

    const QString frameBCanonical = FrameId(frameBPath).canonicalPath();
    const auto metricIt = loaded->metricsByFrameId.find(frameBCanonical);
    ASSERT_NE(metricIt, loaded->metricsByFrameId.end());
    EXPECT_EQ(metricIt->second.metrics.stars, 3);
    EXPECT_DOUBLE_EQ(metricIt->second.metrics.fwhm, 2.9);
    EXPECT_NEAR(metricIt->second.metrics.eccentricity, 0.12, 1e-9);
    EXPECT_TRUE(metricIt->second.metrics.succeeded);
    // Frame noise statistics: the only registration metrics noise weighting can use.
    EXPECT_DOUBLE_EQ(metricIt->second.noise.background_noise, 0.0021);
    EXPECT_DOUBLE_EQ(metricIt->second.noise.global_mad, 0.0031);
    EXPECT_DOUBLE_EQ(metricIt->second.noise.global_scale, 0.0041);

    const auto imageIt = loaded->registeredImagesByFrameId.find(frameBCanonical);
    ASSERT_NE(imageIt, loaded->registeredImagesByFrameId.end());
    EXPECT_FALSE(imageIt->second.failed);
    ASSERT_EQ(imageIt->second.stars.size(), 3u);
    // A star comes back at the engine pixel coordinates it was recorded at.
    for (const auto& [x, y] : {std::pair{15.0, 16.0}, std::pair{22.0, 11.0}, std::pair{18.0, 40.0}})
    {
        EXPECT_TRUE(std::any_of(imageIt->second.stars.begin(), imageIt->second.stars.end(),
                                [x = x, y = y](const RegisteredStar& star) { return star.x == x && star.y == y; }))
            << x << "," << y;
    }
    ASSERT_TRUE(imageIt->second.transform.has_value());
    EXPECT_NEAR(imageIt->second.transform->translation().x, 4.0, 1e-9);
    EXPECT_NEAR(imageIt->second.transform->translation().y, -3.5, 1e-9);
    EXPECT_DOUBLE_EQ(imageIt->second.noise.background_noise, 0.0021);
    EXPECT_DOUBLE_EQ(imageIt->second.noise.global_mad, 0.0031);
    EXPECT_DOUBLE_EQ(imageIt->second.noise.global_scale, 0.0041);

    const auto reuseCheck = an::ui::checkReusableStandaloneRegistrationState(*context, images, {}, &error);
    ASSERT_TRUE(reuseCheck.reusable) << reuseCheck.reason.toStdString() << " / " << error.toStdString();
}

// ---------------------------------------------------------------------------
// Negative-path tests
// ---------------------------------------------------------------------------

// After persisting state, bump the mtime of one input file. The reuse check
// must detect the change and return reusable=false.
TEST_F(TempDirFixture, StandaloneRegistrationPersistenceRejectsReusableAfterMtimeChange)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    // Create the files so mtime can be read
    {
        QFile f(frameAPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly);
    }

    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("M31"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("M31"));
    const std::vector<FrameInfo> images{frameA, frameB};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(frameAPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;
    registeredData.frames.emplace(
        frameAPath, makeRegistered(frameAPath, makeStars({QPointF(1.0, 1.0)}), makeTranslationTransform(0.0, 0.0)));
    registeredData.frames.emplace(
        frameBPath, makeRegistered(frameBPath, makeStars({QPointF(1.0, 1.0)}), makeTranslationTransform(1.0, 0.0)));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    // Touch frameBPath to change its mtime after persist.
    // Sleep >1s because fileMtimeSecs uses second-resolution timestamps.
    QThread::sleep(2);
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly | QIODevice::Append);
        (void)f.write(" ");
    }

    const auto reuseCheck = an::ui::checkReusableStandaloneRegistrationState(*context, images, {}, &error);
    EXPECT_FALSE(reuseCheck.reusable);
    EXPECT_FALSE(reuseCheck.reason.isEmpty());
}

// Changing a registration setting (transformation type) after persisting must
// produce a context whose hashes differ from the stored state, causing
// standaloneRegistrationStateMatchesContext to return false.
TEST_F(TempDirFixture, StandaloneRegistrationPersistenceDetectsSettingsChange)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    {
        QFile f(frameAPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly);
    }

    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("NGC1023"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("NGC1023"));
    const std::vector<FrameInfo> images{frameA, frameB};

    RegistrationSettings settingsV1;
    settingsV1.referenceImage = FrameId(frameAPath).canonicalPath();
    settingsV1.transformation = QStringLiteral("Homography");

    QString error;
    const auto contextV1 =
        an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settingsV1, &error);
    ASSERT_TRUE(contextV1.has_value()) << error.toStdString();

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;
    registeredData.frames.emplace(
        frameAPath, makeRegistered(frameAPath, makeStars({QPointF(5.0, 5.0)}), makeTranslationTransform(0.0, 0.0)));
    registeredData.frames.emplace(
        frameBPath, makeRegistered(frameBPath, makeStars({QPointF(5.0, 5.0)}), makeTranslationTransform(2.0, 0.0)));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*contextV1, images, registeredData, &error))
        << error.toStdString();

    // Load the persisted group state and check it against a context built with a different
    // transformation setting — the match must fail.
    RegistrationSettings settingsV2 = settingsV1;
    settingsV2.transformation = QStringLiteral("Affine"); // changed

    const auto contextV2 =
        an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settingsV2, &error);
    ASSERT_TRUE(contextV2.has_value()) << error.toStdString();

    an::ui::RegistrationMetricsStateLoader loader(contextV1->databasePath, QStringLiteral("SettingsChangeTest"));
    ASSERT_TRUE(loader.open(&error)) << error.toStdString();
    const auto groupState = loader.loadGroupState(contextV1->groupKey, contextV1->settingsVersion, &error);
    ASSERT_TRUE(groupState.has_value()) << error.toStdString();

    EXPECT_FALSE(an::ui::standaloneRegistrationStateMatchesContext(*contextV2, *groupState));
}

// If a frame has been added to the image list after persisting, the star cache
// is incomplete for that frame and checkReusable must return false.
TEST_F(TempDirFixture, StandaloneRegistrationPersistenceRejectsReusableWhenFrameAdded)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    const QString frameCPath = QDir(tempPath()).filePath(QStringLiteral("light_c.fit"));
    {
        QFile f(frameAPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameCPath);
        (void)f.open(QIODevice::WriteOnly);
    }

    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("M45"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("M45"));
    const auto frameC = makeLightFrame(frameCPath, 3000, 2000, QStringLiteral("M45"));

    const std::vector<FrameInfo> originalImages{frameA, frameB};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(frameAPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context =
        an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), originalImages, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;
    registeredData.frames.emplace(
        frameAPath, makeRegistered(frameAPath, makeStars({QPointF(8.0, 8.0)}), makeTranslationTransform(0.0, 0.0)));
    registeredData.frames.emplace(
        frameBPath, makeRegistered(frameBPath, makeStars({QPointF(8.0, 8.0)}), makeTranslationTransform(3.0, 1.0)));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, originalImages, registeredData, &error))
        << error.toStdString();

    // Now check reusability with an expanded frame list that includes frameC
    const std::vector<FrameInfo> expandedImages{frameA, frameB, frameC};
    const auto reuseCheck = an::ui::checkReusableStandaloneRegistrationState(*context, expandedImages, {}, &error);
    EXPECT_FALSE(reuseCheck.reusable);
    EXPECT_FALSE(reuseCheck.reason.isEmpty());
}

// Persisting state where one frame failed must cause checkReusable
// to return false (a failed frame must trigger a re-run).
TEST_F(TempDirFixture, StandaloneRegistrationPersistenceRejectsReusableWhenFrameFailed)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    {
        QFile f(frameAPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly);
    }

    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("M57"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("M57"));
    const std::vector<FrameInfo> images{frameA, frameB};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(frameAPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;
    registeredData.frames.emplace(
        frameAPath, makeRegistered(frameAPath, makeStars({QPointF(10.0, 10.0)}), makeTranslationTransform(0.0, 0.0)));
    // This frame failed registration: no stars and no transform.
    registeredData.frames.emplace(frameBPath, makeRegistered(frameBPath, {}, std::nullopt, true));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    const auto reuseCheck = an::ui::checkReusableStandaloneRegistrationState(*context, images, {}, &error);
    EXPECT_FALSE(reuseCheck.reusable);
    EXPECT_FALSE(reuseCheck.reason.isEmpty());
}

TEST_F(TempDirFixture, StandaloneRegistrationPersistenceAllowsIgnoredFailedFrameReuse)
{
    const QString frameAPath = QDir(tempPath()).filePath(QStringLiteral("light_a.fit"));
    const QString frameBPath = QDir(tempPath()).filePath(QStringLiteral("light_b.fit"));
    {
        QFile f(frameAPath);
        (void)f.open(QIODevice::WriteOnly);
    }
    {
        QFile f(frameBPath);
        (void)f.open(QIODevice::WriteOnly);
    }

    const auto frameA = makeLightFrame(frameAPath, 3000, 2000, QStringLiteral("M57"));
    const auto frameB = makeLightFrame(frameBPath, 3000, 2000, QStringLiteral("M57"));
    const std::vector<FrameInfo> images{frameA, frameB};

    RegistrationSettings settings;
    settings.referenceImage = FrameId(frameAPath).canonicalPath();
    settings.transformation = QStringLiteral("Homography");

    QString error;
    const auto context = an::ui::buildStandaloneRegistrationPersistenceContext(tempPath(), images, settings, &error);
    ASSERT_TRUE(context.has_value()) << error.toStdString();

    RegisteredFrameSet registeredData;
    registeredData.referencePath = frameAPath;
    registeredData.frames.emplace(
        frameAPath, makeRegistered(frameAPath, makeStars({QPointF(10.0, 10.0)}), makeTranslationTransform(0.0, 0.0)));
    registeredData.frames.emplace(frameBPath, makeRegistered(frameBPath, {}, std::nullopt, true));

    ASSERT_TRUE(an::ui::persistStandaloneRegistrationState(*context, images, registeredData, &error))
        << error.toStdString();

    std::unordered_set<QString> ignoredFrameIds{FrameId(frameBPath).canonicalPath()};
    const auto reuseCheck = an::ui::checkReusableStandaloneRegistrationState(*context, images, ignoredFrameIds, &error);
    EXPECT_TRUE(reuseCheck.reusable) << reuseCheck.reason.toStdString();
}
