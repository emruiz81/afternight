// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A file read as a frame of the integration model through the engine's probe: the kind its cards state before its
/// folder, the figures grouping and calibration matching read, the capture time in UTC, the refusals and warnings, and
/// a picked master filed as its tab's kind without the model reading the file again.

#include "Test_ImageIntegration/ImportSession.h"

#include "fixtures/common/IntegrationFrameFiles.h"

#include "processes/ImageIntegration/core/FileImport.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUndoStack>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace
{

using an::tests::fixtures::AcquisitionFacts;
using ImageIntegration::BuildDetectedFrameInfoResult;
using ImageIntegration::FrameTypeEvidence;
using ImageIntegration::ImportAdmission;
using ImageIntegration::ImportIssueSeverity;

class FrameImportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(1u);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        m_session = std::make_unique<an::consumer::EngineSession>(std::move(*opened));
    }

    /// Writes a frame stating `facts` at `relative` below the case's directory and answers its path.
    QString writeFrame(const QString& relative, const AcquisitionFacts& facts)
    {
        const QString path = m_directory.filePath(relative);
        std::string error;
        const bool written = an::tests::fixtures::writeAcquisitionFrame(
            an::tests::fixtures::acquisitionFrame(QFileInfo(relative).fileName().toStdString(), facts, 24u, 16u),
            std::filesystem::path(path.toStdWString()), error);
        EXPECT_TRUE(written) << error;
        return path;
    }

    /// Writes bytes that are no image at `relative`.
    QString writeBytes(const QString& relative, const QByteArray& bytes)
    {
        const QString path = m_directory.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        return path;
    }

    BuildDetectedFrameInfoResult import(const QString& path, const FrameType fallback = FrameType::Light,
                                        const ImportAdmission admission = ImportAdmission::Wait)
    {
        return ImageIntegration::buildDetectedFrameInfoWithIssue(*m_session, path, fallback, admission);
    }

    QTemporaryDir m_directory;
    std::unique_ptr<an::consumer::EngineSession> m_session;
};

[[nodiscard]] AcquisitionFacts kindOnly(const std::string& keyword, const std::string& kind)
{
    AcquisitionFacts facts;
    facts.kindKeyword = keyword;
    facts.kind = kind;
    return facts;
}

TEST_F(FrameImportTest, TheFrameTypeCardOutranksTheFolder)
{
    // A light copied into a folder named for darks is still a light: the folder no longer overrides IMAGETYP.
    const BuildDetectedFrameInfoResult light =
        import(writeFrame("darks/light_001.fit", kindOnly("IMAGETYP", "Light")), FrameType::Bias);
    ASSERT_TRUE(light.detected.has_value());
    EXPECT_FALSE(light.issue.has_value());
    EXPECT_EQ(light.detected->type, FrameType::Light);
    EXPECT_EQ(light.detected->info.metadata.type(), FrameType::Light);
    EXPECT_EQ(light.detected->typeEvidence, FrameTypeEvidence::Card);

    const BuildDetectedFrameInfoResult flat = import(writeFrame("lights/flat_001.fit", kindOnly("FRAME", "Flat")));
    ASSERT_TRUE(flat.detected.has_value());
    EXPECT_EQ(flat.detected->type, FrameType::Flat);
    EXPECT_EQ(flat.detected->typeEvidence, FrameTypeEvidence::Card);

    const BuildDetectedFrameInfoResult bias =
        import(writeFrame("flats/bias_001.fit", kindOnly("IMAGETYP", "Bias Frame")), FrameType::Dark);
    ASSERT_TRUE(bias.detected.has_value());
    EXPECT_EQ(bias.detected->type, FrameType::Bias);
}

TEST_F(FrameImportTest, AFrameWithoutAKindCardIsFiledByItsFolderAndThenByTheCallersKind)
{
    const BuildDetectedFrameInfoResult foldered = import(writeFrame("darks/frame_001.fit", AcquisitionFacts{}));
    ASSERT_TRUE(foldered.detected.has_value());
    EXPECT_EQ(foldered.detected->type, FrameType::Dark);
    EXPECT_EQ(foldered.detected->typeEvidence, FrameTypeEvidence::Folder);

    const BuildDetectedFrameInfoResult unfiled =
        import(writeFrame("session/frame_002.fit", kindOnly("IMAGETYP", "DefectMap")), FrameType::Flat);
    ASSERT_TRUE(unfiled.detected.has_value());
    EXPECT_EQ(unfiled.detected->type, FrameType::Flat);
    EXPECT_EQ(unfiled.detected->typeEvidence, FrameTypeEvidence::Fallback);
}

TEST_F(FrameImportTest, TheFiguresGroupingReadsComeFromTheCards)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Dark Frame");
    facts.exposureSeconds = 120.5;
    facts.sensorTemperatureCelsius = -10.0;
    facts.gain = 139;
    facts.offset = 21;
    facts.filter = "Ha";
    facts.object = "M 42";
    facts.instrument = "ZWO ASI2600MM Pro";
    facts.exposureStart = "2026-01-12T21:15:30";

    const BuildDetectedFrameInfoResult read = import(writeFrame("dark_120s.fit", facts));
    ASSERT_TRUE(read.detected.has_value());
    EXPECT_FALSE(read.issue.has_value());
    const FrameMetadata& metadata = read.detected->info.metadata;
    EXPECT_EQ(metadata.type(), FrameType::Dark);
    EXPECT_EQ(metadata.dimensions(), QSize(24, 16));
    EXPECT_DOUBLE_EQ(metadata.exposureSeconds(), 120.5);
    ASSERT_TRUE(metadata.temperatureC().has_value());
    EXPECT_DOUBLE_EQ(*metadata.temperatureC(), -10.0);
    EXPECT_EQ(metadata.isoGain(), std::optional<int>(139));
    EXPECT_EQ(metadata.offset(), std::optional<int>(21));
    EXPECT_EQ(metadata.filter(), QStringLiteral("Ha"));
    EXPECT_EQ(metadata.objectName(), QStringLiteral("M 42"));
    EXPECT_EQ(metadata.readoutMode(), QStringLiteral("ZWO ASI2600MM Pro"));
    EXPECT_EQ(metadata.capturedAtUtc(), QDateTime(QDate(2026, 1, 12), QTime(21, 15, 30), QTimeZone(QTimeZone::UTC)));
}

TEST_F(FrameImportTest, WhatEachPixelHoldsIsRecorded)
{
    // A mosaic plane, one plane or three channels: what the files a run writes from the frame weigh.
    using an::tests::fixtures::FixtureCfa;
    using an::tests::fixtures::FixtureChannels;
    const auto layoutOf = [&](const QString& relative, const FixtureChannels channels,
                              const FixtureCfa cfa) -> std::optional<FrameSampleLayout> {
        an::tests::fixtures::FixtureImage frame =
            an::tests::fixtures::acquisitionFrame(relative.toStdString(), kindOnly("IMAGETYP", "Light"), 24u, 16u);
        frame.channels = channels;
        frame.cfa = cfa;
        if (channels == FixtureChannels::Rgb)
        {
            frame.levels = {frame.levels.front(), frame.levels.front(), frame.levels.front()};
        }
        const QString path = m_directory.filePath(relative);
        std::string error;
        EXPECT_TRUE(
            an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
            << error;
        const BuildDetectedFrameInfoResult read = import(path);
        return read.detected.has_value() ? read.detected->info.metadata.sampleLayout() : std::nullopt;
    };

    EXPECT_EQ(layoutOf(QStringLiteral("mosaic.fit"), FixtureChannels::Mono, FixtureCfa::Rggb),
              (FrameSampleLayout{.channels = 1, .mosaic = true}));
    EXPECT_EQ(layoutOf(QStringLiteral("mono.fit"), FixtureChannels::Mono, FixtureCfa::None),
              (FrameSampleLayout{.channels = 1, .mosaic = false}));
    EXPECT_EQ(layoutOf(QStringLiteral("colour.fit"), FixtureChannels::Rgb, FixtureCfa::None),
              (FrameSampleLayout{.channels = 3, .mosaic = false}));
}

TEST_F(FrameImportTest, TheIsoSpeedIsTheFigureBeforeTheGainSettingAndTheExposureCardMayBeEither)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Light");
    facts.isoSpeed = 800;
    facts.gain = 100;
    facts.exposureKeyword = "EXPOSURE";
    facts.exposureSeconds = 30.0;

    const BuildDetectedFrameInfoResult read = import(writeFrame("light_iso.fit", facts));
    ASSERT_TRUE(read.detected.has_value());
    EXPECT_EQ(read.detected->info.metadata.isoGain(), std::optional<int>(800));
    EXPECT_DOUBLE_EQ(read.detected->info.metadata.exposureSeconds(), 30.0);
}

TEST_F(FrameImportTest, TheSentinelsCaptureProgramsWriteReadAsNothing)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Light");
    facts.filter = "N/A";
    facts.object = "-";
    facts.instrument = "n/a";

    const BuildDetectedFrameInfoResult read = import(writeFrame("light_sentinels.fit", facts));
    ASSERT_TRUE(read.detected.has_value());
    const FrameMetadata& metadata = read.detected->info.metadata;
    EXPECT_TRUE(metadata.filter().isEmpty());
    EXPECT_TRUE(metadata.objectName().isEmpty());
    EXPECT_TRUE(metadata.readoutMode().isEmpty());
    EXPECT_DOUBLE_EQ(metadata.exposureSeconds(), 0.0);
    EXPECT_FALSE(metadata.temperatureC().has_value());
    EXPECT_FALSE(metadata.isoGain().has_value());
    EXPECT_FALSE(metadata.offset().has_value());
}

// The session a frame is filed under is the UTC day it was taken. DATE, the time the file was written, read as the
// host's local time and converted to UTC, would put the same frame on another day on a host away from UTC; the
// exposure start is read as the UTC instant the card states, on every host.
TEST_F(FrameImportTest, TheCaptureTimeIsTheExposureStartInUtc)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Light");
    facts.exposureStart = "2026-01-12T23:59:30";
    facts.fileDate = "2026-01-13T00:04:31";

    const BuildDetectedFrameInfoResult read = import(writeFrame("light_midnight.fit", facts));
    ASSERT_TRUE(read.detected.has_value());
    const QDateTime expected(QDate(2026, 1, 12), QTime(23, 59, 30), QTimeZone(QTimeZone::UTC));
    EXPECT_EQ(read.detected->info.metadata.capturedAtUtc(), expected);
    EXPECT_EQ(read.detected->info.effectiveSession(), QStringLiteral("2026-01-12"));
}

// A capture program that writes the exposure start in its own zone still states when the frame was taken: the offset
// after the time of day moves the instant to UTC, and the session is that UTC day rather than the day DATE names.
TEST_F(FrameImportTest, AnExposureStartWrittenWithItsZoneIsReadAsTheUtcInstant)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Light");
    facts.exposureStart = "2026-01-13T05:29:30+05:30";
    facts.fileDate = "2026-01-13T05:34:31";

    const BuildDetectedFrameInfoResult read = import(writeFrame("light_zoned.fit", facts));
    ASSERT_TRUE(read.detected.has_value());
    const QDateTime expected(QDate(2026, 1, 12), QTime(23, 59, 30), QTimeZone(QTimeZone::UTC));
    EXPECT_EQ(read.detected->info.metadata.capturedAtUtc(), expected);
    EXPECT_EQ(read.detected->info.effectiveSession(), QStringLiteral("2026-01-12"));
}

TEST_F(FrameImportTest, TheCaptureTimeFallsBackToWhenTheFileWasWrittenAndThenToItsModificationTime)
{
    AcquisitionFacts dated = kindOnly("IMAGETYP", "Light");
    dated.fileDate = "2026-02-03T04:05:06";
    const BuildDetectedFrameInfoResult fromDate = import(writeFrame("light_dated.fit", dated));
    ASSERT_TRUE(fromDate.detected.has_value());
    EXPECT_EQ(fromDate.detected->info.metadata.capturedAtUtc(),
              QDateTime(QDate(2026, 2, 3), QTime(4, 5, 6), QTimeZone(QTimeZone::UTC)));

    const QString undated = writeFrame("light_undated.fit", kindOnly("IMAGETYP", "Light"));
    const BuildDetectedFrameInfoResult fromFile = import(undated);
    ASSERT_TRUE(fromFile.detected.has_value());
    EXPECT_EQ(fromFile.detected->info.metadata.capturedAtUtc(), QFileInfo(undated).lastModified().toUTC());
}

TEST_F(FrameImportTest, AFormatTheSessionDoesNotReadIsRefused)
{
    const BuildDetectedFrameInfoResult text = import(writeBytes("notes.txt", "not an image"));
    EXPECT_FALSE(text.detected.has_value());
    ASSERT_TRUE(text.issue.has_value());
    EXPECT_EQ(text.issue->severity, ImportIssueSeverity::Error);
    EXPECT_EQ(text.issue->message, QStringLiteral("Unsupported file format"));

    // A format the engine knows and this session registered no codec for.
    const BuildDetectedFrameInfoResult png = import(writeBytes("frame.png", "\x89PNG\r\n\x1a\n"));
    EXPECT_FALSE(png.detected.has_value());
    ASSERT_TRUE(png.issue.has_value());
    EXPECT_EQ(png.issue->severity, ImportIssueSeverity::Error);
}

TEST_F(FrameImportTest, AnEmptyOrMissingPathIsRefused)
{
    const BuildDetectedFrameInfoResult empty = import(QString());
    EXPECT_FALSE(empty.detected.has_value());
    ASSERT_TRUE(empty.issue.has_value());
    EXPECT_EQ(empty.issue->severity, ImportIssueSeverity::Error);

    const BuildDetectedFrameInfoResult missing = import(m_directory.filePath(QStringLiteral("absent.fit")));
    EXPECT_FALSE(missing.detected.has_value());
    ASSERT_TRUE(missing.issue.has_value());
    EXPECT_EQ(missing.issue->message, QStringLiteral("File does not exist"));
}

TEST_F(FrameImportTest, AFileThatCannotBeProbedIsImportedWithAWarningAndItsFoldersKind)
{
    const QString corrupt = writeBytes("darks/corrupt.fit", "NOT_A_FITS_FILE");
    const BuildDetectedFrameInfoResult read = import(corrupt, FrameType::Light);
    ASSERT_TRUE(read.detected.has_value());
    ASSERT_TRUE(read.issue.has_value());
    EXPECT_EQ(read.issue->severity, ImportIssueSeverity::Warning);
    EXPECT_TRUE(read.issue->message.startsWith(QStringLiteral("Metadata read failed: ")));
    EXPECT_EQ(read.detected->type, FrameType::Dark);
    EXPECT_EQ(read.detected->typeEvidence, FrameTypeEvidence::Folder);
    EXPECT_EQ(read.detected->info.metadata.dimensions(), QSize(0, 0));
}

TEST_F(FrameImportTest, AProbeThatMayNotWaitIsAWarningWhileTheSessionIsBusy)
{
    const QString path = writeFrame("lights/light_busy.fit", kindOnly("IMAGETYP", "Flat"));
    {
        // The session's one thread is taken; the GUI-thread form of the import does not wait for it.
        an::Result<an::consumer::OperationScope> held =
            m_session->tryAdmit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(held.hasValue()) << held.status().message();

        const BuildDetectedFrameInfoResult refused = import(path, FrameType::Light, ImportAdmission::NoWait);
        ASSERT_TRUE(refused.detected.has_value());
        ASSERT_TRUE(refused.issue.has_value());
        EXPECT_EQ(refused.issue->severity, ImportIssueSeverity::Warning);
        EXPECT_EQ(refused.detected->type, FrameType::Light);
        EXPECT_EQ(refused.detected->typeEvidence, FrameTypeEvidence::Folder);
    }

    const BuildDetectedFrameInfoResult read = import(path, FrameType::Light, ImportAdmission::NoWait);
    ASSERT_TRUE(read.detected.has_value());
    EXPECT_FALSE(read.issue.has_value());
    EXPECT_EQ(read.detected->type, FrameType::Flat);
}

// An import probes many files at once from its own pool, so each probe takes one of the session's threads rather than
// every thread its purpose may have, which would run the probes one after another.
TEST(FrameImportProbeTest, TwoProbesAreAdmittedAtOnceOnTwoThreads)
{
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    const an::consumer::EngineSession& session = *opened;

    const an::consumer::OperationRequest request = ImageIntegration::importProbeRequest();
    EXPECT_EQ(request.purpose, an::consumer::OperationPurpose::Batch);
    an::Result<an::consumer::OperationScope> first = session.tryAdmit(request);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    an::Result<an::consumer::OperationScope> second = session.tryAdmit(request);
    EXPECT_TRUE(second.hasValue()) << second.status().message();
}

// A master the user picks for a tab is that tab's kind whatever its cards say, keeps the figures a calibration match
// can use, and is added from the frame its caller read - redoing the step reads no file.
TEST_F(FrameImportTest, APickedMasterIsFiledAsItsTabsKindFromTheFrameItsCallerRead)
{
    AcquisitionFacts facts = kindOnly("IMAGETYP", "Light");
    facts.exposureSeconds = 2.0;
    facts.sensorTemperatureCelsius = 150.0;
    facts.gain = 0;
    facts.offset = 30;
    facts.filter = "L";
    facts.exposureStart = "2026-03-04T05:06:07";
    const QString path = writeFrame("masters/master_flat_L.fit", facts);

    const BuildDetectedFrameInfoResult read = import(path, FrameType::Flat);
    ASSERT_TRUE(read.detected.has_value());

    IntegrationDataModel model;
    QUndoStack stack;
    stack.push(new AddExternalMasterCommand(&model, FrameType::Flat, read.detected->info));

    ASSERT_EQ(model.flatFrames().size(), 1u);
    EXPECT_TRUE(model.lightFrames().empty());
    const std::optional<FrameInfo> master = model.flatFrames().findById(FrameId(path));
    ASSERT_TRUE(master.has_value());
    EXPECT_EQ(master->metadata.type(), FrameType::Flat);
    EXPECT_DOUBLE_EQ(master->metadata.exposureSeconds(), 2.0);
    EXPECT_FALSE(master->metadata.temperatureC().has_value());
    EXPECT_FALSE(master->metadata.isoGain().has_value());
    EXPECT_EQ(master->metadata.offset(), std::optional<int>(30));
    EXPECT_EQ(master->userSession, QStringLiteral("2026-03-04"));
    EXPECT_EQ(model.masterSource(FrameId(path)), std::optional<MasterSource>(MasterSource::UserDefined));

    stack.undo();
    EXPECT_TRUE(model.flatFrames().empty());

    ASSERT_TRUE(QFile::remove(path));
    stack.redo();
    ASSERT_EQ(model.flatFrames().size(), 1u);
    EXPECT_EQ(model.masterSource(FrameId(path)), std::optional<MasterSource>(MasterSource::UserDefined));
}

} // namespace
