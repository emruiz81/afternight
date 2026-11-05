// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence companions the file workflows, the process framework and the history reach: a SER, an image list and a
/// video opened as a document showing their first frame, and recording what their files say of them for the summary; a
/// sequence written to each container its path names, whole or not at all; the frames a process reads - the selection
/// in order, every frame without one, a mosaic - an X-Trans one among them - demosaiced by the sequence's method when
/// asked; and the sequence a process writes, published only when finished.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "app/FileCompanions.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProcessCompanions.h"
#include "controls/SequenceContainers.h"

#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/SerSequence.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <stop_token>

namespace
{

using namespace an::tests::sequences;

/// Reads every frame a reader hands out, as the run's worker does.
[[nodiscard]] std::vector<an::ui::ProcessSequenceFrame> readAll(const an::consumer::EngineSession& session,
                                                                an::ui::IProcessFrameReader& reader)
{
    std::vector<an::ui::ProcessSequenceFrame> frames;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    for (;;)
    {
        an::Result<std::optional<an::ui::ProcessSequenceFrame>> next = reader.next(scope->engine(), scope->execution());
        EXPECT_TRUE(next.hasValue()) << next.status().message();
        if (!next.hasValue() || !next->has_value())
        {
            break;
        }
        frames.push_back(std::move(**next));
    }
    return frames;
}

/// The description of an asset.
[[nodiscard]] an::ImageDescriptor descriptorOf(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    return *lease->visit([](const auto& typed) { return typed.descriptor(); });
}

} // namespace

// A SER opens as a document that states the sequence - its file, its container, its count - and holds its first frame,
// named after the file.
TEST(SequenceCompanionsTest, ASerOpensAsADocumentShowingItsFirstFrame)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 3u};
    const QString path = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Ser, fixture));

    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context->engineSession(), path, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    const std::optional<an::ui::DocumentSequence> sequence = document->sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->sourcePath, path);
    EXPECT_EQ(sequence->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(sequence->descriptor.frameCount, 3u);
    EXPECT_EQ(sequence->currentFrame, 0u);
    EXPECT_FALSE(sequence->selectedFrames.has_value());
    EXPECT_EQ(document->names().displayName, QStringLiteral("capture"));
    EXPECT_EQ(document->names().sourcePath, path);
    EXPECT_EQ(largestDifference(samplesOf(*document->asset()), fixtureSamples(0u, fixture, false)), 0.0);
}

// An image list and a video open as documents too, each stating its container.
TEST(SequenceCompanionsTest, AnImageListAndAVideoOpenAsDocuments)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 2u, .channels = Channels::Rgb};
    const QString avi = context->pathOf(QStringLiteral("capture.avi"));
    ASSERT_TRUE(writeFixture(context->engineSession(), avi, an::SequenceFormat::Avi, fixture));
    an::Result<an::ui::ImageDocument> video =
        an::ui::openSequenceDocument(context->engineSession(), avi, an::SequenceFormat::Avi, {});
    ASSERT_TRUE(video.hasValue()) << video.status().message();
    EXPECT_EQ(video->sequence()->descriptor.format, an::SequenceFormat::Avi);
    EXPECT_EQ(video->sequence()->descriptor.frameCount, 2u);
    EXPECT_EQ(largestDifference(samplesOf(*video->asset()), fixtureSamples(0u, fixture, true)), 0.0);

    const QString list = context->pathOf(QStringLiteral("list.anseq"));
    {
        const an::Status checked = an::ui::writeSequenceFile(context->engineSession(), *video->sequence(), list, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    an::Result<an::ui::ImageDocument> images =
        an::ui::openSequenceDocument(context->engineSession(), list, an::SequenceFormat::ImageList, {});
    ASSERT_TRUE(images.hasValue()) << images.status().message();
    EXPECT_EQ(images->sequence()->descriptor.format, an::SequenceFormat::ImageList);
    EXPECT_EQ(images->sequence()->descriptor.frameCount, 2u);
    EXPECT_EQ(largestDifference(samplesOf(*images->asset()), fixtureSamples(0u, fixture, true)), 0.0);
}

// A sequence opened as a document records what its file says of it, for the summary: when a SER was recorded - its
// header's time, or its first frame's when the header states none - and for an image list when it was made, its first
// frame's description and the size of every file it names. Nothing recorded them, so the summary showed the list
// file's own size, and '-' for the date and for an image list's frames.
TEST(SequenceCompanionsTest, ASequenceDocumentRecordsWhenItWasRecordedAndAnImageListsFramesAndFiles)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QDateTime when(QDate(2026, 3, 4), QTime(5, 6, 7), QTimeZone(QTimeZone::UTC));
    const an::UtcInstant recorded{when.toSecsSinceEpoch() * 1000000000};

    FixtureSequence dated{.frames = 2u};
    dated.serDateTimeUtcTicks = an::serTicksFromUtcInstant(recorded);
    const QString capture = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), capture, an::SequenceFormat::Ser, dated));
    an::Result<an::ui::ImageDocument> ser =
        an::ui::openSequenceDocument(context->engineSession(), capture, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(ser.hasValue()) << ser.status().message();
    EXPECT_EQ(ser->sequence()->recorded, std::optional<an::UtcInstant>(recorded)) << "the header's time";
    EXPECT_TRUE(ser->sequence()->descriptor.fixedFrame.has_value());
    EXPECT_FALSE(ser->sequence()->firstFrame.has_value()) << "a SER states the frame every frame is held to";
    EXPECT_FALSE(ser->sequence()->namedFilesBytes.has_value());

    const QString timed = context->pathOf(QStringLiteral("timed.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), timed, an::SequenceFormat::Ser,
                             FixtureSequence{.frames = 2u, .trailerInterval = 400000}));
    an::Result<an::ui::ImageDocument> stamped =
        an::ui::openSequenceDocument(context->engineSession(), timed, an::SequenceFormat::Ser, {});
    ASSERT_TRUE(stamped.hasValue()) << stamped.status().message();
    EXPECT_EQ(stamped->sequence()->recorded, an::utcInstantFromSerTicks(kFixtureTimelineOrigin))
        << "the first frame's time, when the header states none";

    an::ImageListManifest manifest;
    manifest.timestampSeconds = static_cast<std::uint64_t>(when.toSecsSinceEpoch());
    std::uint64_t named = 0u;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const QString still = context->pathOf(QStringLiteral("still-%1.fits").arg(frame));
        ASSERT_TRUE(writeFitsStill(context->engineSession(), still, frame, 64u, 48u, Channels::Grey));
        named += static_cast<std::uint64_t>(QFileInfo(still).size());
        manifest.framePaths.push_back(QFileInfo(still).fileName().toStdString());
    }
    const QString list = context->pathOf(QStringLiteral("list.anseq"));
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
        ASSERT_TRUE(scope.has_value());
        const std::string destination = list.toStdString();
        const an::SequenceOutputRequest request{.destinationPath = destination};
        ASSERT_TRUE(an::writeImageListManifest(scope->engine(), scope->execution(), request, manifest).hasValue());
    }
    ASSERT_NE(static_cast<std::uint64_t>(QFileInfo(list).size()), named)
        << "the case tells the named files' size from the list's own";
    an::Result<an::ui::ImageDocument> images =
        an::ui::openSequenceDocument(context->engineSession(), list, an::SequenceFormat::ImageList, {});
    ASSERT_TRUE(images.hasValue()) << images.status().message();
    const std::optional<an::ui::DocumentSequence> sequence = images->sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->recorded, std::optional<an::UtcInstant>(recorded)) << "the manifest's time";
    EXPECT_EQ(sequence->namedFilesBytes, std::optional<std::uint64_t>(named));
    EXPECT_FALSE(sequence->descriptor.fixedFrame.has_value()) << "no frame of the list is held to the first";
    ASSERT_TRUE(sequence->firstFrame.has_value());
    EXPECT_EQ(sequence->firstFrame->extent().width(), 64u);
    EXPECT_EQ(sequence->firstFrame->extent().height(), 48u);
    EXPECT_EQ(sequence->firstFrame->residentSampleType(), std::optional<an::SampleType>(an::SampleType::UInt16));
}

// A sequence that holds no frame, a file that is not one and a container nothing reads are refused, and a stopped open
// is cancelled.
TEST(SequenceCompanionsTest, AnEmptyOrUnreadableSequenceIsRefusedAndAStoppedOpenIsCancelled)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString empty = context->pathOf(QStringLiteral("empty.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), empty, an::SequenceFormat::Ser, FixtureSequence{.frames = 0u}));
    an::Result<an::ui::ImageDocument> none =
        an::ui::openSequenceDocument(context->engineSession(), empty, an::SequenceFormat::Ser, {});
    ASSERT_FALSE(none.hasValue());
    EXPECT_EQ(none.status().code(), an::ErrorCode::EmptySelection);

    const QString junk = context->pathOf(QStringLiteral("junk.ser"));
    {
        QFile file(junk);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a sequence");
    }
    EXPECT_FALSE(an::ui::openSequenceDocument(context->engineSession(), junk, an::SequenceFormat::Ser, {}).hasValue());

    an::Result<an::ui::ImageDocument> fits =
        an::ui::openSequenceDocument(context->engineSession(), junk, an::SequenceFormat::Fits, {});
    ASSERT_FALSE(fits.hasValue());
    EXPECT_EQ(fits.status().code(), an::ErrorCode::Unsupported);

    std::stop_source stop;
    stop.request_stop();
    an::Result<an::ui::ImageDocument> stopped =
        an::ui::openSequenceDocument(context->engineSession(), empty, an::SequenceFormat::Ser, stop.get_token());
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
}

// A sequence is written to the container its path names: an image list keeps sixteen-bit frames exactly, AVI holds
// them at eight bits at corresponding full scales, and a video holds every frame at its size.
TEST(SequenceCompanionsTest, ASequenceIsWrittenToTheContainerItsPathNames)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 3u};
    const QString source = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), source, an::SequenceFormat::Ser, fixture));
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = source;
    sequence.descriptor.format = an::SequenceFormat::Ser;

    const QString list = context->pathOf(QStringLiteral("copy.anseq"));
    {
        const an::Status checked = an::ui::writeSequenceFile(context->engineSession(), sequence, list, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> listed = readBack(context->engineSession(), list, an::SequenceFormat::ImageList);
    ASSERT_TRUE(listed.has_value());
    ASSERT_EQ(listed->samples.size(), 3u);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        EXPECT_EQ(largestDifference(listed->samples[frame], fixtureSamples(frame, fixture, false)), 0.0);
    }

    const QString avi = context->pathOf(QStringLiteral("copy.avi"));
    {
        const an::Status checked = an::ui::writeSequenceFile(context->engineSession(), sequence, avi, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> video = readBack(context->engineSession(), avi, an::SequenceFormat::Avi);
    ASSERT_TRUE(video.has_value());
    ASSERT_EQ(video->samples.size(), 3u);
    const std::vector<double> wide = fixtureSamples(2u, fixture, false);
    for (std::size_t i = 0u; i < wide.size(); ++i)
    {
        EXPECT_EQ(video->samples[2][i], std::round(wide[i] * 255.0 / 65535.0)) << "sample " << i;
    }

    if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
    {
        const QString mp4 = context->pathOf(QStringLiteral("copy.mp4"));
        {
            const an::Status checked = an::ui::writeSequenceFile(context->engineSession(), sequence, mp4, {});
            ASSERT_TRUE(checked.ok()) << checked.message();
        }
        const std::optional<ReadBack> encoded = readBack(context->engineSession(), mp4, an::SequenceFormat::Mp4);
        ASSERT_TRUE(encoded.has_value());
        ASSERT_EQ(encoded->frames.size(), 3u);
        EXPECT_EQ(encoded->frames[0].toImageDescriptor()->extent().width(), fixture.width);
        EXPECT_EQ(encoded->frames[0].toImageDescriptor()->extent().height(), fixture.height);
    }
}

// A write that is stopped, whose source is gone or whose path names no container leaves nothing at its path.
TEST(SequenceCompanionsTest, AWriteThatCannotCompleteLeavesNothingAtItsPath)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString source = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), source, an::SequenceFormat::Ser, FixtureSequence{}));
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = source;
    sequence.descriptor.format = an::SequenceFormat::Ser;

    std::stop_source stop;
    stop.request_stop();
    const an::Status stopped =
        an::ui::writeSequenceFile(context->engineSession(), sequence, context->pathOf("stopped.ser"), stop.get_token());
    EXPECT_EQ(stopped.code(), an::ErrorCode::Cancelled);

    const an::Status unnamed =
        an::ui::writeSequenceFile(context->engineSession(), sequence, context->pathOf("copy.fits"), {});
    EXPECT_EQ(unnamed.code(), an::ErrorCode::Unsupported);

    an::ui::DocumentSequence gone = sequence;
    gone.sourcePath = context->pathOf(QStringLiteral("gone.ser"));
    EXPECT_TRUE(an::ui::writeSequenceFile(context->engineSession(), gone, context->pathOf("copy.ser"), {}).failed());

    EXPECT_EQ(filesIn(context->directory()), QStringList{QStringLiteral("capture.ser")});
}

// A process reads the selected frames in the sequence's order, and every frame when nothing is selected.
TEST(SequenceCompanionsTest, AProcessReadsTheSelectedFramesOrEveryFrame)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 4u};
    const QString path = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Ser, fixture));
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 4u;

    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> every =
        an::ui::openProcessFrames(context->engineSession(), sequence, false);
    ASSERT_TRUE(every.hasValue()) << every.status().message();
    EXPECT_EQ((*every)->frameCount(), 4u);
    const std::vector<an::ui::ProcessSequenceFrame> all = readAll(context->engineSession(), **every);
    ASSERT_EQ(all.size(), 4u);
    for (std::uint32_t frame = 0u; frame < 4u; ++frame)
    {
        EXPECT_EQ(all[frame].index, frame);
        EXPECT_EQ(largestDifference(samplesOf(all[frame].asset), fixtureSamples(frame, fixture, false)), 0.0);
    }

    sequence.selectedFrames = std::vector<std::uint64_t>{1u, 3u};
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> selected =
        an::ui::openProcessFrames(context->engineSession(), sequence, false);
    ASSERT_TRUE(selected.hasValue());
    EXPECT_EQ((*selected)->frameCount(), 2u);
    const std::vector<an::ui::ProcessSequenceFrame> chosen = readAll(context->engineSession(), **selected);
    ASSERT_EQ(chosen.size(), 2u);
    EXPECT_EQ(chosen[0].index, 1u);
    EXPECT_EQ(chosen[1].index, 3u);
    EXPECT_EQ(largestDifference(samplesOf(chosen[1].asset), fixtureSamples(3u, fixture, false)), 0.0);

    sequence.descriptor.format = an::SequenceFormat::Fits;
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> refused =
        an::ui::openProcessFrames(context->engineSession(), sequence, false);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
}

// A mosaic frame a process reads is demosaiced when the sequence view demosaics, by the sequence's own method, and is
// left as recorded otherwise.
TEST(SequenceCompanionsTest, AMosaicFrameIsDemosaicedByTheSequencesMethodWhenAsked)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    const QString path = context->pathOf(QStringLiteral("mosaic.ser"));
    ASSERT_TRUE(
        writeFixture(context->engineSession(), path, an::SequenceFormat::Ser,
                     FixtureSequence{.frames = 2u, .width = 32u, .height = 24u, .cfa = *rggb, .textured = true}));
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = path;
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.demosaic = an::DemosaicMethod::Bilinear;

    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> raw =
        an::ui::openProcessFrames(context->engineSession(), sequence, false);
    ASSERT_TRUE(raw.hasValue());
    const std::vector<an::ui::ProcessSequenceFrame> mosaics = readAll(context->engineSession(), **raw);
    ASSERT_EQ(mosaics.size(), 2u);
    EXPECT_TRUE(descriptorOf(mosaics[0].asset).isMosaiced());

    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> colour =
        an::ui::openProcessFrames(context->engineSession(), sequence, true);
    ASSERT_TRUE(colour.hasValue());
    const std::vector<an::ui::ProcessSequenceFrame> demosaiced = readAll(context->engineSession(), **colour);
    ASSERT_EQ(demosaiced.size(), 2u);
    EXPECT_EQ(descriptorOf(demosaiced[1].asset).channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_FALSE(descriptorOf(demosaiced[1].asset).isMosaiced());

    // The same frame demosaiced by the method the sequence states, straight through the viewer's demosaic.
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mosaics[1].asset.read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::AnyImage> expected =
        an::ui::demosaicViewFrame(scope->engine(), scope->execution(), lease->image(), an::DemosaicMethod::Bilinear);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    // The texture is one where the setting's default method demosaics otherwise, so the case tells the two apart.
    an::Result<an::consumer::AnyImage> byDefault =
        an::ui::demosaicViewFrame(scope->engine(), scope->execution(), lease->image(), an::DemosaicMethod::Rcd);
    ASSERT_TRUE(byDefault.hasValue()) << byDefault.status().message();
    an::Result<an::consumer::ImageAsset> expectedAsset =
        an::consumer::ImageAsset::adopt(context->engineSession(), std::move(*expected));
    ASSERT_TRUE(expectedAsset.hasValue());
    an::Result<an::consumer::ImageAsset> defaultAsset =
        an::consumer::ImageAsset::adopt(context->engineSession(), std::move(*byDefault));
    ASSERT_TRUE(defaultAsset.hasValue());
    ASSERT_GT(largestDifference(samplesOf(*defaultAsset), samplesOf(*expectedAsset)), 0.0);
    EXPECT_EQ(largestDifference(samplesOf(demosaiced[1].asset), samplesOf(*expectedAsset)), 0.0);
}

// The sequence a process writes takes its source's container and is published only when the run finishes it; a writer
// let go before that leaves nothing at its path.
TEST(SequenceCompanionsTest, AProcessOutputIsPublishedOnlyWhenFinished)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 2u};
    an::ui::DocumentSequence source;
    source.sourcePath = context->pathOf(QStringLiteral("capture.ser"));
    source.descriptor.format = an::SequenceFormat::Ser;
    source.demosaic = an::DemosaicMethod::Vng4;
    ASSERT_TRUE(writeFixture(context->engineSession(), source.sourcePath, an::SequenceFormat::Ser, fixture));
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
        an::ui::openProcessFrames(context->engineSession(), source, false);
    ASSERT_TRUE(reader.hasValue());
    const std::vector<an::ui::ProcessSequenceFrame> frames = readAll(context->engineSession(), **reader);
    ASSERT_EQ(frames.size(), 2u);

    const QString output = context->pathOf(QStringLiteral("processed.ser"));
    {
        an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> abandoned =
            an::ui::createProcessFrameOutput(context->engineSession(), source, output);
        ASSERT_TRUE(abandoned.hasValue()) << abandoned.status().message();
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
        an::Result<an::consumer::AssetReadLease> lease = frames[0].asset.read();
        ASSERT_TRUE((*abandoned)->write(scope->engine(), scope->execution(), 0u, lease->image()).ok());
    }
    EXPECT_FALSE(QFileInfo::exists(output));
    EXPECT_EQ(filesIn(context->directory()), QStringList{QStringLiteral("capture.ser")});

    an::Result<std::unique_ptr<an::ui::IProcessFrameWriter>> writer =
        an::ui::createProcessFrameOutput(context->engineSession(), source, output);
    ASSERT_TRUE(writer.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    for (const an::ui::ProcessSequenceFrame& frame : frames)
    {
        an::Result<an::consumer::AssetReadLease> lease = frame.asset.read();
        ASSERT_TRUE((*writer)->write(scope->engine(), scope->execution(), frame.index, lease->image()).ok());
        EXPECT_FALSE(QFileInfo::exists(output));
    }
    an::Result<an::ui::DocumentSequence> finished = (*writer)->finish(scope->engine(), scope->execution());
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    scope.reset();
    EXPECT_EQ(finished->sourcePath, output);
    EXPECT_EQ(finished->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(finished->descriptor.frameCount, 2u);
    EXPECT_EQ(finished->demosaic, an::DemosaicMethod::Vng4);
    const std::optional<ReadBack> read = readBack(context->engineSession(), output, an::SequenceFormat::Ser);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->samples.size(), 2u);
    EXPECT_EQ(largestDifference(read->samples[1], fixtureSamples(1u, fixture, false)), 0.0);

    // A source no container of the product writes cannot make an output.
    an::ui::DocumentSequence fits = source;
    fits.descriptor.format = an::SequenceFormat::Fits;
    EXPECT_FALSE(
        an::ui::createProcessFrameOutput(context->engineSession(), fits, context->pathOf("x.fits")).hasValue());
}

// An X-Trans frame a process reads is demosaiced, when the sequence view demosaics, by the sequence's method's
// counterpart for a six-by-six table - the X-Trans estimate for the bilinear one - and is left as recorded otherwise.
// Every method a sequence states estimates a two-by-two block, which the engine refuses over an X-Trans frame, so a
// process over such a sequence failed at its first frame.
TEST(SequenceCompanionsTest, AnXTransFrameIsDemosaicedByTheTabulatedCounterpartOfTheSequencesMethod)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString list = writeXTransList(*context, 2u);
    ASSERT_FALSE(list.isEmpty());
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = list;
    sequence.descriptor.format = an::SequenceFormat::ImageList;
    sequence.demosaic = an::DemosaicMethod::Bilinear;

    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> raw =
        an::ui::openProcessFrames(context->engineSession(), sequence, false);
    ASSERT_TRUE(raw.hasValue()) << raw.status().message();
    const std::vector<an::ui::ProcessSequenceFrame> mosaics = readAll(context->engineSession(), **raw);
    ASSERT_EQ(mosaics.size(), 2u);
    EXPECT_TRUE(descriptorOf(mosaics[0].asset).isMosaiced());
    EXPECT_TRUE(descriptorOf(mosaics[0].asset).cfa().hasFilterTable()) << "the stills keep their six-by-six table";

    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> colour =
        an::ui::openProcessFrames(context->engineSession(), sequence, true);
    ASSERT_TRUE(colour.hasValue()) << colour.status().message();
    const std::vector<an::ui::ProcessSequenceFrame> demosaiced = readAll(context->engineSession(), **colour);
    ASSERT_EQ(demosaiced.size(), 2u);
    EXPECT_EQ(descriptorOf(demosaiced[1].asset).channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_FALSE(descriptorOf(demosaiced[1].asset).isMosaiced());

    // The same frame demosaiced by the X-Trans estimate straight through the viewer's demosaic, and by three-pass
    // Markesteijn, which demosaics this texture otherwise, so the case tells the two apart.
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mosaics[1].asset.read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::AnyImage> expected =
        an::ui::demosaicViewFrame(scope->engine(), scope->execution(), lease->image(), an::DemosaicMethod::XTransFast);
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    an::Result<an::consumer::AnyImage> threePass = an::ui::demosaicViewFrame(
        scope->engine(), scope->execution(), lease->image(), an::DemosaicMethod::MarkesteijnThreePass);
    ASSERT_TRUE(threePass.hasValue()) << threePass.status().message();
    an::Result<an::consumer::ImageAsset> expectedAsset =
        an::consumer::ImageAsset::adopt(context->engineSession(), std::move(*expected));
    ASSERT_TRUE(expectedAsset.hasValue());
    an::Result<an::consumer::ImageAsset> threePassAsset =
        an::consumer::ImageAsset::adopt(context->engineSession(), std::move(*threePass));
    ASSERT_TRUE(threePassAsset.hasValue());
    ASSERT_GT(largestDifference(samplesOf(*threePassAsset), samplesOf(*expectedAsset)), 0.0);
    EXPECT_EQ(largestDifference(samplesOf(demosaiced[1].asset), samplesOf(*expectedAsset)), 0.0);
}
