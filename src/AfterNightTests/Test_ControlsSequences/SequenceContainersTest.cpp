// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence containers: which the product reads and writes; a SER, an image list and an AVI opened as sources whose
/// frames read back as written, in any order, and a saved image list opened with its entries in the order it states; a
/// container nothing reads refused by name; and the writer - converting a frame for its container, cropping a video
/// frame to an even size, keeping a mosaic in SER and writing it to AVI as grey, keeping each frame's exposure start in
/// a SER and its cards in an image list, and publishing its container only when its last frame is in, or leaving
/// nothing at all.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "controls/SequenceContainers.h"

#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <QFile>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::sequences;
using an::ui::OpenedSequence;
using an::ui::SequenceWriter;
using an::ui::SequenceWriteSettings;

/// An admitted operation of a session for a case that calls the containers directly.
[[nodiscard]] an::consumer::OperationScope admit(const an::consumer::EngineSession& session)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    return std::move(*scope);
}

/// A frame of a representation as an image of an admitted operation's context.
template <typename SampleT>
[[nodiscard]] an::consumer::AnyImage
frameOf(const an::consumer::OperationScope& scope, std::uint32_t width, std::uint32_t height, Channels channels,
        const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
        an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    an::Result<an::Image<SampleT>> image = fixtureImage<SampleT>(scope.engine(), width, height, channels, value, cfa);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    return an::consumer::AnyImage(std::move(*image));
}

/// The samples of an image, as doubles, channel by channel and row by row.
[[nodiscard]] std::vector<double> imageSamples(const an::consumer::AnyImage& image)
{
    std::vector<double> values;
    std::visit(
        [&](const auto& typed) {
            const auto view = typed.view();
            const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
            for (std::uint32_t channel = 0u; channel < descriptor->extent().channelCount(); ++channel)
            {
                for (std::uint32_t y = 0u; y < descriptor->extent().height(); ++y)
                {
                    for (std::uint32_t x = 0u; x < descriptor->extent().width(); ++x)
                    {
                        values.push_back(sampleAt(*view, x, y, channel));
                    }
                }
            }
        },
        image);
    return values;
}

/// The description of an image.
[[nodiscard]] an::ImageDescriptor descriptorOf(const an::consumer::AnyImage& image)
{
    return *std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// A card of a FITS header.
[[nodiscard]] an::CodecCard fitsCard(std::string keyword, std::string value)
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(an::CodecNamespace::Fits, std::move(keyword), std::move(value), {});
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return std::move(*made);
}

/// The value of a FITS card a metadata's file stated, or nothing.
[[nodiscard]] std::optional<std::string> fitsCardValue(const an::ImageMetadata& metadata, std::string_view keyword)
{
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (card.origin() == an::CodecNamespace::Fits && card.keyword() == keyword)
        {
            return card.value();
        }
    }
    return std::nullopt;
}

/// Reads a sequence through the product's own source and appends every frame to a writer of the settings, finishing it;
/// fails the case when it cannot.
[[nodiscard]] bool copySequence(const an::consumer::EngineSession& session, const QString& source,
                                an::SequenceFormat format, const QString& destination,
                                const SequenceWriteSettings& settings)
{
    an::consumer::OperationScope scope = admit(session);
    an::Result<OpenedSequence> opened = an::ui::openSequenceSource(scope.engine(), scope.execution(), source, format);
    EXPECT_TRUE(opened.hasValue()) << opened.status().message();
    if (!opened.hasValue())
    {
        return false;
    }
    an::Result<SequenceWriter> writer = SequenceWriter::create(destination, settings);
    EXPECT_TRUE(writer.hasValue()) << writer.status().message();
    if (!writer.hasValue())
    {
        return false;
    }
    for (;;)
    {
        auto read = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, std::nullopt);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue())
        {
            return false;
        }
        if (!read->has_value())
        {
            break;
        }
        const an::Status appended = writer->append(scope.engine(), scope.execution(), (*read)->second);
        EXPECT_TRUE(appended.ok()) << appended.message();
        if (appended.failed())
        {
            return false;
        }
    }
    const an::Result<an::SequenceDescriptor> finished = writer->finish(scope.execution());
    EXPECT_TRUE(finished.hasValue()) << finished.status().message();
    return finished.hasValue();
}

} // namespace

// The product reads SER, image lists and the video containers, writes SER, image lists, AVI, MP4, Matroska and WebM,
// and names a container for writing by its path - never a FITS frame container or generated frames.
TEST(SequenceContainersTest, TheProductReadsAndWritesTheContainersItNames)
{
    EXPECT_TRUE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Ser));
    EXPECT_TRUE(an::ui::isReadableSequenceContainer(an::SequenceFormat::ImageList));
    EXPECT_TRUE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Avi));
    EXPECT_TRUE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Mp4));
    EXPECT_TRUE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Video));
    EXPECT_FALSE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Fits));
    EXPECT_FALSE(an::ui::isReadableSequenceContainer(an::SequenceFormat::Dummy));
    EXPECT_FALSE(an::ui::isWritableSequenceContainer(an::SequenceFormat::Fits));
    EXPECT_FALSE(an::ui::isWritableSequenceContainer(an::SequenceFormat::Dummy));

    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("a/b.SER")), an::SequenceFormat::Ser);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.anseq")), an::SequenceFormat::ImageList);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.avi")), an::SequenceFormat::Avi);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.mp4")), an::SequenceFormat::Mp4);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.mov")), an::SequenceFormat::Mp4);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.mkv")), an::SequenceFormat::Video);
    EXPECT_EQ(an::ui::writableSequenceContainerFor(QStringLiteral("b.webm")), an::SequenceFormat::Video);
    EXPECT_FALSE(an::ui::writableSequenceContainerFor(QStringLiteral("b.mpg")).has_value());
    EXPECT_FALSE(an::ui::writableSequenceContainerFor(QStringLiteral("b.fits")).has_value());
    EXPECT_FALSE(an::ui::writableSequenceContainerFor(QStringLiteral("b")).has_value());
}

// A frame rate is an exact fraction: a whole rate as itself and any other to a thousandth, one frame a second for a
// rate that is not one.
TEST(SequenceContainersTest, AFrameRateIsAnExactFraction)
{
    EXPECT_EQ(an::ui::sequenceRateOf(25.0).numerator, 25u);
    EXPECT_EQ(an::ui::sequenceRateOf(25.0).denominator, 1u);
    EXPECT_EQ(an::ui::sequenceRateOf(29.97).numerator, 2997u);
    EXPECT_EQ(an::ui::sequenceRateOf(29.97).denominator, 100u);
    EXPECT_EQ(an::ui::sequenceRateOf(2.5).numerator, 5u);
    EXPECT_EQ(an::ui::sequenceRateOf(2.5).denominator, 2u);
    EXPECT_EQ(an::ui::sequenceRateOf(0.0).numerator, 1u);
    EXPECT_EQ(an::ui::sequenceRateOf(std::nan("")).denominator, 1u);
    EXPECT_DOUBLE_EQ(an::ui::framesPerSecondOf(an::SequenceRate{2997u, 100u}), 29.97);
}

// A SER opens as a source that states its frames, and each frame reads back exactly as written, in any order.
TEST(SequenceContainersTest, ASerOpensAsASourceAndReadsEachFrameAsWrittenInAnyOrder)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 4u, .width = 9u, .height = 5u};
    const QString path = context->pathOf(QStringLiteral("capture.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Ser, fixture));

    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<OpenedSequence> opened =
        an::ui::openSequenceSource(scope.engine(), scope.execution(), path, an::SequenceFormat::Ser);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    EXPECT_EQ(opened->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(opened->descriptor.frameCount, 4u);
    for (const std::uint64_t frame : {2u, 0u, 3u, 1u})
    {
        auto read = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, frame);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->has_value());
        EXPECT_EQ((*read)->first, frame);
        EXPECT_EQ(descriptorOf((*read)->second).sampleType(), an::SampleType::UInt16);
        EXPECT_EQ(largestDifference(imageSamples((*read)->second),
                                    fixtureSamples(static_cast<std::uint32_t>(frame), fixture, false)),
                  0.0)
            << "frame " << frame;
    }
    // Reading on from the last frame finds the end.
    auto last = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, 3u);
    ASSERT_TRUE(last.hasValue());
    ASSERT_TRUE(last->has_value());
    EXPECT_EQ((*last)->first, 3u);
    auto end = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, std::nullopt);
    ASSERT_TRUE(end.hasValue());
    EXPECT_FALSE(end->has_value());
}

// An image list opens from its manifest, each frame a file it names, with the frame rate the manifest states.
TEST(SequenceContainersTest, AnImageListOpensFromItsManifestWithItsFrameRate)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    an::ImageListManifest manifest;
    manifest.framesPerSecond = 2.5;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const QString still = context->pathOf(QStringLiteral("still-%1.fits").arg(frame));
        ASSERT_TRUE(writeFitsStill(context->engineSession(), still, frame, 7u, 4u, Channels::Rgb));
        manifest.framePaths.push_back(QFileInfo(still).fileName().toStdString());
    }
    const QString path = context->pathOf(QStringLiteral("list.anseq"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        const std::string destination = path.toStdString();
        ASSERT_TRUE(an::writeImageListManifest(scope.engine(), scope.execution(),
                                               an::SequenceOutputRequest{.destinationPath = destination}, manifest)
                        .hasValue());
    }

    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<OpenedSequence> opened =
        an::ui::openSequenceSource(scope.engine(), scope.execution(), path, an::SequenceFormat::ImageList);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    EXPECT_EQ(opened->descriptor.frameCount, 3u);
    ASSERT_TRUE(opened->descriptor.nominalFrameRate.has_value());
    EXPECT_EQ(opened->descriptor.nominalFrameRate->numerator, 5u);
    EXPECT_EQ(opened->descriptor.nominalFrameRate->denominator, 2u);
    const FixtureSequence still{.width = 7u, .height = 4u, .channels = Channels::Rgb};
    for (const std::uint64_t frame : {1u, 2u, 0u})
    {
        auto read = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, frame);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->has_value());
        EXPECT_EQ(largestDifference(imageSamples((*read)->second),
                                    fixtureSamples(static_cast<std::uint32_t>(frame), still, false)),
                  0.0);
    }
}

// An image list as the product saves it - File > Save as... over a sequence of four files added in a stated order -
// opens as the product opens it: every entry, in the order the person added them, spelled as the list states it -
// relative to the list, its frames beside it, one name escaped in the XML - each frame the file it names, and the
// list's frame rate and creation time as it stated them.
TEST(SequenceContainersTest, ASavedImageListOpensWithItsEntriesInOrder)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString directory = QFileInfo(context->pathOf(QStringLiteral("list.anseq"))).absolutePath();
    // The four files were added in this order; the number in each file's samples names it.
    const std::vector<std::pair<std::string, std::uint32_t>> entries{{"saved list-frames/frame_b.fits", 1u},
                                                                     {"saved list-frames/frame_d.fits", 3u},
                                                                     {"saved list-frames/frame c \xC3\xB1&.fits", 2u},
                                                                     {"saved list-frames/frame_a.fits", 0u}};
    ASSERT_TRUE(QDir(directory).mkpath(QStringLiteral("saved list-frames")));
    an::ImageListManifest saved;
    saved.timestampSeconds = 1792375566u;
    saved.width = 16u;
    saved.height = 12u;
    saved.channels = 1u;
    saved.framesPerSecond = 1.0;
    for (const auto& entry : entries)
    {
        const std::uint32_t named = entry.second;
        ASSERT_TRUE(writeFitsStill(context->engineSession(),
                                   QDir(directory).filePath(QString::fromStdString(entry.first)), named, 16u, 12u,
                                   Channels::Grey, an::CfaDescriptor{}, {},
                                   [named](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                       return static_cast<std::uint16_t>(1000u * (named + 1u) + 16u * y + x);
                                   }))
            << entry.first;
        saved.framePaths.push_back(entry.first);
    }
    const QString path = QDir(directory).filePath(QStringLiteral("saved list.anseq"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        const std::string destination = path.toUtf8().toStdString();
        ASSERT_TRUE(an::writeImageListManifest(scope.engine(), scope.execution(),
                                               an::SequenceOutputRequest{.destinationPath = destination}, saved)
                        .hasValue());
    }

    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<an::ImageListManifest> manifest =
        an::readImageListManifest(scope.engine(), scope.execution(), path.toStdString());
    ASSERT_TRUE(manifest.hasValue()) << manifest.status().message();
    std::vector<std::string> spelled;
    for (const auto& entry : entries)
    {
        spelled.push_back(entry.first);
    }
    EXPECT_EQ(manifest->framePaths, spelled);
    EXPECT_EQ(manifest->timestampSeconds, 1792375566u);
    EXPECT_EQ(manifest->width, 16u);
    EXPECT_EQ(manifest->height, 12u);
    EXPECT_EQ(manifest->channels, 1u);
    EXPECT_FALSE(manifest->mosaiced);
    EXPECT_DOUBLE_EQ(manifest->framesPerSecond, 1.0);

    an::Result<OpenedSequence> opened =
        an::ui::openSequenceSource(scope.engine(), scope.execution(), path, an::SequenceFormat::ImageList);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    EXPECT_EQ(opened->descriptor.frameCount, entries.size());
    ASSERT_TRUE(opened->descriptor.nominalFrameRate.has_value());
    EXPECT_EQ(opened->descriptor.nominalFrameRate->numerator, 1u);
    EXPECT_EQ(opened->descriptor.nominalFrameRate->denominator, 1u);
    ASSERT_TRUE(opened->created.has_value());
    EXPECT_EQ(opened->created->nanosecondsSinceEpoch, 1792375566LL * 1000000000LL);
    ASSERT_EQ(opened->framePaths.size(), entries.size());
    for (std::size_t index = 0u; index < entries.size(); ++index)
    {
        EXPECT_EQ(QFileInfo(QString::fromStdString(opened->framePaths[index])).canonicalFilePath(),
                  QFileInfo(QDir(directory).filePath(QString::fromStdString(entries[index].first))).canonicalFilePath())
            << index;
    }
    for (const std::uint64_t frame : {2u, 0u, 3u, 1u})
    {
        auto read = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, frame);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->has_value());
        EXPECT_EQ((*read)->first, frame);
        const std::vector<double> samples = imageSamples((*read)->second);
        ASSERT_EQ(samples.size(), 16u * 12u);
        const double named = 1000.0 * (entries[frame].second + 1u);
        for (std::uint32_t y = 0u; y < 12u; ++y)
        {
            for (std::uint32_t x = 0u; x < 16u; ++x)
            {
                ASSERT_EQ(samples[y * 16u + x], named + 16.0 * y + x) << "frame " << frame << " at " << x << ", " << y;
            }
        }
    }
}

// An uncompressed AVI opens through FFmpeg and its eight-bit colour frames read back exactly.
TEST(SequenceContainersTest, AnAviOpensThroughFfmpegAndItsFramesReadBackExactly)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const FixtureSequence fixture{.frames = 3u, .width = 8u, .height = 6u, .channels = Channels::Rgb};
    const QString path = context->pathOf(QStringLiteral("capture.avi"));
    ASSERT_TRUE(writeFixture(context->engineSession(), path, an::SequenceFormat::Avi, fixture));

    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<OpenedSequence> opened =
        an::ui::openSequenceSource(scope.engine(), scope.execution(), path, an::SequenceFormat::Avi);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    EXPECT_EQ(opened->descriptor.frameCount, 3u);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        auto read = an::ui::readSequenceFrame(scope.engine(), scope.execution(), opened->source, std::nullopt);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->has_value());
        EXPECT_EQ(descriptorOf((*read)->second).channelSchema(), an::ChannelSchema::Rgb);
        EXPECT_EQ(largestDifference(imageSamples((*read)->second), fixtureSamples(frame, fixture, true)), 0.0);
    }
}

// A FITS frame container and generated frames are refused by name: no source of the engine reads one from a file.
TEST(SequenceContainersTest, AContainerNothingReadsIsRefusedByName)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    an::consumer::OperationScope scope = admit(context->engineSession());
    for (const an::SequenceFormat format : {an::SequenceFormat::Fits, an::SequenceFormat::Dummy})
    {
        an::Result<OpenedSequence> opened =
            an::ui::openSequenceSource(scope.engine(), scope.execution(), context->pathOf("frames.fits"), format);
        ASSERT_FALSE(opened.hasValue());
        EXPECT_EQ(opened.status().code(), an::ErrorCode::Unsupported);
    }
    an::Result<SequenceWriter> writer = SequenceWriter::create(
        context->pathOf("frames.fits"), SequenceWriteSettings{.container = an::SequenceFormat::Fits});
    ASSERT_FALSE(writer.hasValue());
    EXPECT_EQ(writer.status().code(), an::ErrorCode::Unsupported);
}

// A writer publishes nothing at its path until its last frame is in and it finishes; then the container holds every
// frame as written.
TEST(SequenceContainersTest, AWriterPublishesNothingUntilItFinishes)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString path = context->pathOf(QStringLiteral("written.ser"));
    an::Result<SequenceWriter> writer =
        SequenceWriter::create(path, SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = 2u});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    an::consumer::OperationScope scope = admit(context->engineSession());
    const FixtureSequence fixture{.frames = 2u, .width = 6u, .height = 4u};
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        const an::Status appended =
            writer->append(scope.engine(), scope.execution(),
                           frameOf<std::uint16_t>(scope, 6u, 4u, Channels::Grey,
                                                  [frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                                      return fixtureSample(frame, x, y, c);
                                                  }));
        ASSERT_TRUE(appended.ok()) << appended.message();
        EXPECT_FALSE(QFileInfo::exists(path)) << "published before it finished";
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope.execution());
    ASSERT_TRUE(published.hasValue()) << published.status().message();
    EXPECT_TRUE(writer->published());
    EXPECT_EQ(published->frameCount, 2u);
    scope.end();
    EXPECT_EQ(filesIn(context->directory()), QStringList{QStringLiteral("written.ser")});
    const std::optional<ReadBack> read = readBack(context->engineSession(), path, an::SequenceFormat::Ser);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->samples.size(), 2u);
    EXPECT_EQ(largestDifference(read->samples[1], fixtureSamples(1u, fixture, false)), 0.0);
}

// A writer let go before it finishes leaves nothing at its path and nothing beside it, in every container: no pending
// payload, no image list's frames, no frames directory.
TEST(SequenceContainersTest, AWriterLetGoBeforeItFinishesLeavesNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    struct Case
    {
        QString name;
        an::SequenceFormat container;
    };
    std::vector<Case> cases{{QStringLiteral("let-go.ser"), an::SequenceFormat::Ser},
                            {QStringLiteral("let-go.anseq"), an::SequenceFormat::ImageList},
                            {QStringLiteral("let-go.avi"), an::SequenceFormat::Avi}};
    if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
    {
        cases.push_back({QStringLiteral("let-go.mp4"), an::SequenceFormat::Mp4});
    }
    for (const Case& item : cases)
    {
        {
            an::Result<SequenceWriter> writer =
                SequenceWriter::create(context->pathOf(item.name), SequenceWriteSettings{.container = item.container});
            ASSERT_TRUE(writer.hasValue()) << writer.status().message();
            an::consumer::OperationScope scope = admit(context->engineSession());
            for (std::uint32_t frame = 0u; frame < 2u; ++frame)
            {
                const an::Status appended = writer->append(
                    scope.engine(), scope.execution(),
                    frameOf<std::uint8_t>(scope, 8u, 6u, Channels::Grey,
                                          [frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                              return static_cast<std::uint8_t>(fixtureSample(frame, x, y, c) % 256u);
                                          }));
                ASSERT_TRUE(appended.ok()) << item.name.toStdString() << ": " << appended.message();
            }
            EXPECT_FALSE(filesIn(context->directory()).isEmpty()) << item.name.toStdString() << " wrote nothing";
        }
        EXPECT_TRUE(filesIn(context->directory()).isEmpty())
            << item.name.toStdString() << " left " << filesIn(context->directory()).join(", ").toStdString();
    }
}

// A writer aborted while it is still held leaves nothing at once, and refuses every frame and finish after.
TEST(SequenceContainersTest, AWriterAbortedWhileHeldLeavesNothingAndWritesNothingMore)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    for (const QString& name : {QStringLiteral("aborted.ser"), QStringLiteral("aborted.anseq")})
    {
        const an::SequenceFormat container =
            name.endsWith(QStringLiteral(".ser")) ? an::SequenceFormat::Ser : an::SequenceFormat::ImageList;
        an::Result<SequenceWriter> writer =
            SequenceWriter::create(context->pathOf(name), SequenceWriteSettings{.container = container});
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();
        an::consumer::OperationScope scope = admit(context->engineSession());
        const auto frame = [&scope]() {
            return frameOf<std::uint16_t>(scope, 8u, 6u, Channels::Grey,
                                          [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                              return static_cast<std::uint16_t>(fixtureSample(0u, x, y, c));
                                          });
        };
        ASSERT_TRUE(writer->append(scope.engine(), scope.execution(), frame()).ok());
        ASSERT_FALSE(filesIn(context->directory()).isEmpty()) << name.toStdString() << " wrote nothing";

        writer->abort();
        EXPECT_TRUE(filesIn(context->directory()).isEmpty())
            << name.toStdString() << " left " << filesIn(context->directory()).join(", ").toStdString();
        EXPECT_EQ(writer->append(scope.engine(), scope.execution(), frame()).code(), an::ErrorCode::InvalidState);
        EXPECT_FALSE(writer->finish(scope.execution()).hasValue());
        EXPECT_FALSE(writer->published());
        EXPECT_TRUE(filesIn(context->directory()).isEmpty()) << name.toStdString();
        writer->abort();
    }
}

// A finish with no frame written is refused and leaves nothing.
TEST(SequenceContainersTest, AFinishWithNoFrameIsRefusedAndLeavesNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    an::Result<SequenceWriter> writer = SequenceWriter::create(
        context->pathOf("empty.ser"), SequenceWriteSettings{.container = an::SequenceFormat::Ser});
    ASSERT_TRUE(writer.hasValue());
    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<an::SequenceDescriptor> published = writer->finish(scope.execution());
    ASSERT_FALSE(published.hasValue());
    EXPECT_EQ(published.status().code(), an::ErrorCode::EmptySelection);
    EXPECT_TRUE(filesIn(context->directory()).isEmpty());
}

// A frame is converted for its container at corresponding full scales: SER takes sixteen bits of a floating or signed
// frame and keeps eight-bit and sixteen-bit ones as they are; AVI takes eight bits of anything.
TEST(SequenceContainersTest, AFrameIsConvertedForItsContainer)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    an::consumer::OperationScope scope = admit(context->engineSession());

    const QString ser = context->pathOf(QStringLiteral("float.ser"));
    {
        an::Result<SequenceWriter> writer = SequenceWriter::create(ser, SequenceWriteSettings{});
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer
                        ->append(scope.engine(), scope.execution(),
                                 frameOf<float>(scope, 4u, 2u, Channels::Grey,
                                                [](std::uint32_t x, std::uint32_t, std::uint32_t) {
                                                    return static_cast<float>(x) / 3.0f;
                                                }))
                        .ok());
        ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
    }
    const QString avi = context->pathOf(QStringLiteral("wide.avi"));
    {
        an::Result<SequenceWriter> writer =
            SequenceWriter::create(avi, SequenceWriteSettings{.container = an::SequenceFormat::Avi});
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer
                        ->append(scope.engine(), scope.execution(),
                                 frameOf<std::uint16_t>(scope, 4u, 2u, Channels::Rgb,
                                                        [](std::uint32_t x, std::uint32_t, std::uint32_t c) {
                                                            return static_cast<std::uint16_t>(257u * (x * 60u + c));
                                                        }))
                        .ok());
        ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
    }
    scope.end();

    const std::optional<ReadBack> serRead = readBack(context->engineSession(), ser, an::SequenceFormat::Ser);
    ASSERT_TRUE(serRead.has_value());
    ASSERT_EQ(serRead->frames.size(), 1u);
    EXPECT_EQ(serRead->frames[0].toImageDescriptor()->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(serRead->samples[0],
              (std::vector<double>{0.0, 21845.0, 43690.0, 65535.0, 0.0, 21845.0, 43690.0, 65535.0}));

    const std::optional<ReadBack> aviRead = readBack(context->engineSession(), avi, an::SequenceFormat::Avi);
    ASSERT_TRUE(aviRead.has_value());
    ASSERT_EQ(aviRead->samples.size(), 1u);
    std::vector<double> expected;
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 4u; ++x)
            {
                expected.push_back(static_cast<double>(x * 60u + c));
            }
        }
    }
    EXPECT_EQ(aviRead->samples[0], expected);
}

// A video stores even widths and heights: a frame with an odd one is cropped by its last column or row.
TEST(SequenceContainersTest, AVideoFrameOfAnOddSizeIsCroppedToAnEvenOne)
{
    if (!an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H264))
    {
        GTEST_SKIP() << "this build has no H.264 encoder";
    }
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString path = context->pathOf(QStringLiteral("odd.mp4"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        an::Result<SequenceWriter> writer =
            SequenceWriter::create(path, SequenceWriteSettings{.container = an::SequenceFormat::Mp4});
        ASSERT_TRUE(writer.hasValue());
        for (int frame = 0; frame < 2; ++frame)
        {
            ASSERT_TRUE(writer
                            ->append(scope.engine(), scope.execution(),
                                     frameOf<std::uint8_t>(
                                         scope, 17u, 11u, Channels::Grey,
                                         [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{128}; }))
                            .ok());
        }
        ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
    }
    const std::optional<ReadBack> read = readBack(context->engineSession(), path, an::SequenceFormat::Mp4);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->frames.size(), 2u);
    EXPECT_EQ(read->frames[0].toImageDescriptor()->extent().width(), 16u);
    EXPECT_EQ(read->frames[0].toImageDescriptor()->extent().height(), 10u);
}

// A mosaic is kept in SER as the mosaic it is, its filter array stated, and written to AVI as its grey samples.
TEST(SequenceContainersTest, AMosaicIsKeptInSerAndWrittenToAviAsGrey)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    const QString ser = context->pathOf(QStringLiteral("mosaic.ser"));
    const QString avi = context->pathOf(QStringLiteral("mosaic.avi"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        for (const auto& [path, container] :
             {std::pair{ser, an::SequenceFormat::Ser}, std::pair{avi, an::SequenceFormat::Avi}})
        {
            an::Result<SequenceWriter> writer =
                SequenceWriter::create(path, SequenceWriteSettings{.container = container});
            ASSERT_TRUE(writer.hasValue());
            ASSERT_TRUE(writer
                            ->append(scope.engine(), scope.execution(),
                                     frameOf<std::uint8_t>(
                                         scope, 8u, 6u, Channels::Grey,
                                         [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                             return static_cast<std::uint8_t>(x * 10u + y);
                                         },
                                         *rggb))
                            .ok());
            ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
        }
    }
    const std::optional<ReadBack> serRead = readBack(context->engineSession(), ser, an::SequenceFormat::Ser);
    ASSERT_TRUE(serRead.has_value());
    EXPECT_TRUE(serRead->frames[0].toImageDescriptor()->isMosaiced());
    EXPECT_EQ(serRead->frames[0].toImageDescriptor()->cfa().pattern(), an::CfaPattern::Rggb);
    // FFmpeg reads the grey bitmap through its grey palette as RGB, every channel the grey samples.
    const std::optional<ReadBack> aviRead = readBack(context->engineSession(), avi, an::SequenceFormat::Avi);
    ASSERT_TRUE(aviRead.has_value());
    EXPECT_FALSE(aviRead->frames[0].toImageDescriptor()->isMosaiced());
    ASSERT_GE(aviRead->samples[0].size(), serRead->samples[0].size());
    EXPECT_EQ(
        std::vector<double>(aviRead->samples[0].begin(),
                            aviRead->samples[0].begin() + static_cast<std::ptrdiff_t>(serRead->samples[0].size())),
        serRead->samples[0]);
}

// An image list's frames are written in their own representation beside its manifest, and removing the list removes
// the manifest and the frames directory.
TEST(SequenceContainersTest, AnImageListKeepsEachFramesRepresentationAndIsRemovedWhole)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString path = context->pathOf(QStringLiteral("frames.anseq"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        an::Result<SequenceWriter> writer =
            SequenceWriter::create(path, SequenceWriteSettings{.container = an::SequenceFormat::ImageList});
        ASSERT_TRUE(writer.hasValue());
        for (int frame = 0; frame < 2; ++frame)
        {
            ASSERT_TRUE(writer
                            ->append(scope.engine(), scope.execution(),
                                     frameOf<float>(scope, 5u, 3u, Channels::Grey,
                                                    [frame](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                                        return 0.01f * static_cast<float>(frame * 100 + x + y * 5u);
                                                    }))
                            .ok());
        }
        ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
    }
    const std::optional<ReadBack> read = readBack(context->engineSession(), path, an::SequenceFormat::ImageList);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->frames.size(), 2u);
    EXPECT_EQ(read->frames[1].toImageDescriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_FLOAT_EQ(static_cast<float>(read->samples[1][6]), 0.01f * 106.0f);
    EXPECT_EQ(filesIn(context->directory()),
              (QStringList{QStringLiteral("frames-frames"), QStringLiteral("frames.anseq")}));

    ASSERT_TRUE(an::ui::removeSequenceFromDisk(path).ok());
    EXPECT_TRUE(filesIn(context->directory()).isEmpty());
    EXPECT_TRUE(an::ui::removeSequenceFromDisk(path).ok()) << "a list already gone is removed already";
}

// A SER the writer makes keeps each frame's exposure start in its trailer - a frame read from a timed SER carries its
// own - so a SER converted, saved or exported reads back at the rate those times give. The writer asked for no trailer,
// so every SER it made kept no timing at all and played at one frame a second.
TEST(SequenceContainersTest, ASerWrittenKeepsEachFramesExposureStartAndItsRate)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    // Four frames 0.04 seconds apart: 25 frames a second.
    const FixtureSequence fixture{.frames = 4u, .width = 6u, .height = 4u, .trailerInterval = 400000};
    const QString source = context->pathOf(QStringLiteral("timed.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), source, an::SequenceFormat::Ser, fixture));
    const std::optional<ReadBack> original = readBack(context->engineSession(), source, an::SequenceFormat::Ser);
    ASSERT_TRUE(original.has_value());
    const std::vector<std::optional<an::UtcInstant>> starts = exposureStartsOf(*original);
    ASSERT_EQ(starts.size(), 4u);
    for (const std::optional<an::UtcInstant>& start : starts)
    {
        ASSERT_TRUE(start.has_value()) << "the fixture's trailer states every frame's exposure start";
    }
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        an::Result<OpenedSequence> opened =
            an::ui::openSequenceSource(scope.engine(), scope.execution(), source, an::SequenceFormat::Ser);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        ASSERT_TRUE(opened->descriptor.nominalFrameRate.has_value()) << "the source states the rate its trailer gives";
        EXPECT_EQ(opened->descriptor.nominalFrameRate->numerator, 25u);
        EXPECT_EQ(opened->descriptor.nominalFrameRate->denominator, 1u);
    }

    const QString copy = context->pathOf(QStringLiteral("copy.ser"));
    ASSERT_TRUE(copySequence(context->engineSession(), source, an::SequenceFormat::Ser, copy,
                             SequenceWriteSettings{.container = an::SequenceFormat::Ser}));
    const std::optional<ReadBack> written = readBack(context->engineSession(), copy, an::SequenceFormat::Ser);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(exposureStartsOf(*written), starts);
    ASSERT_EQ(written->samples.size(), 4u);
    EXPECT_EQ(largestDifference(written->samples[3], fixtureSamples(3u, fixture, false)), 0.0);
    an::consumer::OperationScope scope = admit(context->engineSession());
    an::Result<OpenedSequence> reopened =
        an::ui::openSequenceSource(scope.engine(), scope.execution(), copy, an::SequenceFormat::Ser);
    ASSERT_TRUE(reopened.hasValue()) << reopened.status().message();
    ASSERT_TRUE(reopened->descriptor.nominalFrameRate.has_value()) << "the copy reads back at the same rate";
    EXPECT_EQ(reopened->descriptor.nominalFrameRate->numerator, 25u);
    EXPECT_EQ(reopened->descriptor.nominalFrameRate->denominator, 1u);
}

// An image list the writer makes keeps each frame's header cards - its object, filter, exposure, date and world
// coordinates - in the FITS file it writes for the frame, and a frame of a timed SER its trailer's time as its date.
// The writer opened the list with no intent for its frames, which wrote each through the encoding that carries no
// cards, so a list a sequence was saved or converted to lost every frame's facts.
TEST(SequenceContainersTest, AnImageListWrittenKeepsEachFramesCards)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const std::vector<an::CodecCard> cards{
        fitsCard("OBJECT", "'M 42'"),     fitsCard("FILTER", "'Ha'"),
        fitsCard("EXPTIME", "30.0"),      fitsCard("DATE-OBS", "'2026-10-18T01:02:03'"),
        fitsCard("CTYPE1", "'RA---TAN'"), fitsCard("CTYPE2", "'DEC--TAN'"),
        fitsCard("CUNIT1", "'deg'"),      fitsCard("CUNIT2", "'deg'"),
        fitsCard("CRPIX1", "4.0"),        fitsCard("CRPIX2", "2.5"),
        fitsCard("CRVAL1", "83.82"),      fitsCard("CRVAL2", "-5.39"),
        fitsCard("CD1_1", "-0.00028"),    fitsCard("CD1_2", "0.0"),
        fitsCard("CD2_1", "0.0"),         fitsCard("CD2_2", "0.00028"),
        fitsCard("RADESYS", "'ICRS'")};
    an::ImageListManifest manifest;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const QString still = context->pathOf(QStringLiteral("still-%1.fits").arg(frame));
        ASSERT_TRUE(writeFitsStill(context->engineSession(), still, frame, 7u, 4u, Channels::Grey, {}, cards));
        manifest.framePaths.push_back(QFileInfo(still).fileName().toStdString());
    }
    const QString list = context->pathOf(QStringLiteral("stills.anseq"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        const std::string destination = list.toStdString();
        ASSERT_TRUE(an::writeImageListManifest(scope.engine(), scope.execution(),
                                               an::SequenceOutputRequest{.destinationPath = destination}, manifest)
                        .hasValue());
    }
    // The stills state what the case looks for: their object, filter, exposure, date and solution.
    const std::optional<ReadBack> stills = readBack(context->engineSession(), list, an::SequenceFormat::ImageList);
    ASSERT_TRUE(stills.has_value());
    ASSERT_EQ(stills->metadata.size(), 3u);
    const an::ImageMetadata& stated = stills->metadata[0];
    const std::optional<std::string> object = fitsCardValue(stated, "OBJECT");
    ASSERT_TRUE(object.has_value());
    ASSERT_NE(object->find("M 42"), std::string::npos) << *object;
    ASSERT_TRUE(fitsCardValue(stated, "FILTER").has_value());
    ASSERT_EQ(stated.acquisition().exposureSeconds(), std::optional<double>(30.0));
    ASSERT_TRUE(stated.acquisition().exposureStart().has_value());
    ASSERT_TRUE(stated.astrometry().present());

    const QString copy = context->pathOf(QStringLiteral("copy.anseq"));
    ASSERT_TRUE(copySequence(context->engineSession(), list, an::SequenceFormat::ImageList, copy,
                             SequenceWriteSettings{.container = an::SequenceFormat::ImageList}));
    const std::optional<ReadBack> written = readBack(context->engineSession(), copy, an::SequenceFormat::ImageList);
    ASSERT_TRUE(written.has_value());
    ASSERT_EQ(written->metadata.size(), 3u);
    for (std::size_t frame = 0u; frame < 3u; ++frame)
    {
        const an::ImageMetadata& metadata = written->metadata[frame];
        EXPECT_EQ(fitsCardValue(metadata, "OBJECT"), object) << "frame " << frame;
        EXPECT_EQ(fitsCardValue(metadata, "FILTER"), fitsCardValue(stated, "FILTER")) << "frame " << frame;
        EXPECT_EQ(metadata.acquisition().exposureSeconds(), stated.acquisition().exposureSeconds())
            << "frame " << frame;
        EXPECT_EQ(metadata.acquisition().exposureStart(), stated.acquisition().exposureStart()) << "frame " << frame;
        ASSERT_TRUE(metadata.astrometry().present()) << "frame " << frame;
        const std::optional<an::SkyCoordinate> centre = metadata.astrometry().referencePoint();
        ASSERT_TRUE(centre.has_value());
        EXPECT_NEAR(centre->rightAscensionDegrees, 83.82, 1e-6);
        EXPECT_NEAR(centre->declinationDegrees, -5.39, 1e-6);
        EXPECT_EQ(largestDifference(written->samples[frame],
                                    fixtureSamples(static_cast<std::uint32_t>(frame),
                                                   FixtureSequence{.width = 7u, .height = 4u}, false)),
                  0.0);
    }

    // A frame of a timed SER states its trailer's time, and the list's FITS frame keeps it as its date.
    const QString timed = context->pathOf(QStringLiteral("timed.ser"));
    ASSERT_TRUE(writeFixture(context->engineSession(), timed, an::SequenceFormat::Ser,
                             FixtureSequence{.frames = 2u, .trailerInterval = 400000}));
    const QString converted = context->pathOf(QStringLiteral("timed.anseq"));
    ASSERT_TRUE(copySequence(context->engineSession(), timed, an::SequenceFormat::Ser, converted,
                             SequenceWriteSettings{.container = an::SequenceFormat::ImageList}));
    const std::optional<ReadBack> frames = readBack(context->engineSession(), converted, an::SequenceFormat::ImageList);
    ASSERT_TRUE(frames.has_value());
    ASSERT_FALSE(frames->metadata.empty());
    EXPECT_EQ(frames->metadata[0].acquisition().exposureStart(), an::utcInstantFromSerTicks(kFixtureTimelineOrigin));
}

// An image list's frames are written in its first frame's representation: the list's frames are written under an
// intent that converts nothing, so a later frame of another representation is converted for it at corresponding full
// scales - sixteen-bit codes as their fractions of full scale in a Float32 list.
TEST(SequenceContainersTest, AnImageListOfMixedRepresentationsIsWrittenInItsFirstFramesRepresentation)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const QString path = context->pathOf(QStringLiteral("mixed.anseq"));
    {
        an::consumer::OperationScope scope = admit(context->engineSession());
        an::Result<SequenceWriter> writer =
            SequenceWriter::create(path, SequenceWriteSettings{.container = an::SequenceFormat::ImageList});
        ASSERT_TRUE(writer.hasValue());
        ASSERT_TRUE(writer
                        ->append(scope.engine(), scope.execution(),
                                 frameOf<float>(scope, 5u, 3u, Channels::Grey,
                                                [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                                    return 0.01f * static_cast<float>(x + y * 5u);
                                                }))
                        .ok());
        const an::Status second = writer->append(
            scope.engine(), scope.execution(),
            frameOf<std::uint16_t>(scope, 5u, 3u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return static_cast<std::uint16_t>(1000u * (x + y * 5u));
            }));
        ASSERT_TRUE(second.ok()) << second.message();
        ASSERT_TRUE(writer->finish(scope.execution()).hasValue());
    }
    const std::optional<ReadBack> read = readBack(context->engineSession(), path, an::SequenceFormat::ImageList);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->frames.size(), 2u);
    EXPECT_EQ(read->frames[0].toImageDescriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(read->frames[1].toImageDescriptor()->sampleType(), an::SampleType::Float32);
    ASSERT_EQ(read->samples[1].size(), 15u);
    EXPECT_FLOAT_EQ(static_cast<float>(read->samples[1][7]), static_cast<float>(7000.0 / 65535.0));
}
