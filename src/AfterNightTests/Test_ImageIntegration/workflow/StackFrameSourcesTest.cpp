// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights stacking stage's frame source read by bands. A stack whose frames are files it combines as they are read
/// reads them a band of rows at a time, through a row reader per frame, into the slot the stack lends each read; its
/// bands are the rows of the whole read bit for bit, in mono and colour, with a sample that is not a number read as an
/// exact zero where an exact zero means nothing; a frame the bands cannot read keeps the whole-frame form and says why;
/// a payload too short for a band fails that band naming the frame, and the stack with it; a stop is seen at the next
/// band; a file that changed while the stack read it is refused by name; and the frames past the readers held open
/// read each band through a reader of their own.

#include "Test_ImageIntegration/workflow/CalibrationRun.h"

#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/StackFrameSources.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using an::tests::integration::CalibrationRun;
using stage_utils::StackFrameBandPlan;
using stage_utils::StackFrameBands;
using stage_utils::StackFrameRead;
using stage_utils::WorkflowStackFrameSource;

constexpr std::uint32_t kWidth = 40u;
constexpr std::uint32_t kHeight = 30u;

class StackFrameSourcesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        ASSERT_TRUE(m_run.valid());
    }

    /// Writes a star field of the stated representation, channels and filter array, with the samples stated over it.
    QString
    writeFrame(const QString& name, std::uint64_t seed,
               an::tests::fixtures::FixtureChannels channels = an::tests::fixtures::FixtureChannels::Mono,
               an::tests::fixtures::FixtureSampleType sampleType = an::tests::fixtures::FixtureSampleType::Float32,
               const std::vector<an::tests::fixtures::FixtureSampleOverride>& overrides = {},
               an::tests::fixtures::FixtureCfa cfa = an::tests::fixtures::FixtureCfa::None)
    {
        an::tests::fixtures::FixtureImage image;
        image.name = name.toStdString();
        image.width = kWidth;
        image.height = kHeight;
        image.channels = channels;
        image.sampleType = sampleType;
        image.cfa = cfa;
        image.pattern = an::tests::fixtures::FixturePattern::StarField;
        image.seed = seed;
        an::tests::fixtures::FixtureChannelLevels levels;
        levels.base = 0.1;
        levels.amplitude = 0.05;
        levels.alternate = 1.0;
        image.levels.assign(channels == an::tests::fixtures::FixtureChannels::Rgb ? 3u : 1u, levels);
        image.overrides = overrides;
        image.cards = {{.keyword = "OBJECT", .value = "NGC 3372"}};
        std::string error;
        const auto samples = an::tests::fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        const QString path = QDir(m_root.path()).filePath(name + QStringLiteral(".fits"));
        EXPECT_TRUE(
            an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error))
            << error;
        return path;
    }

    /// The frame a FITS file holds, written again as XISF in Zstandard with its bytes shuffled, in subblocks of the
    /// size stated, with its cards.
    QString compressedCopy(const QString& fits, const QString& name, const std::uint64_t subblockBytes)
    {
        const QString path = QDir(m_root.path()).filePath(name + QStringLiteral(".xisf"));
        an::Result<an::consumer::OperationScope> scope = admit();
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

    /// The frames as a stack plans them: each file held to the revision it has now.
    void plan(const std::vector<QString>& paths)
    {
        m_frames.clear();
        for (const QString& path : paths)
        {
            StackFrameRead read;
            read.logical = FrameId(path);
            read.path = path;
            read.revision = ArtifactTracker::captureFileRevision(path);
            m_frames.push_back(std::move(read));
        }
    }

    /// An operation of the run's session, stopped by the stop source when one is given.
    an::Result<an::consumer::OperationScope> admit(const std::stop_token stop = {})
    {
        return m_run.session().admit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 2u, .stopToken = stop});
    }

    /// A source over the planned frames: whole frames read as the stage reads a light it neither calibrates nor warps,
    /// and bands when `bands` is set.
    std::unique_ptr<WorkflowStackFrameSource> source(const an::consumer::OperationScope& scope, const bool bands,
                                                     const std::uint32_t mostOpenReaders = 128u)
    {
        StackFrameBandPlan plan;
        if (bands)
        {
            StackFrameBands reads{
                .scope = &scope, .registry = nullptr, .frames = {}, .mostOpenReaders = mostOpenReaders};
            for (const StackFrameRead& frame : m_frames)
            {
                reads.frames.push_back(&frame);
            }
            plan.bands = std::move(reads);
        }
        return std::make_unique<WorkflowStackFrameSource>(
            [this, &scope](const std::uint32_t index) {
                return stage_utils::readRegistrationImage(scope, nullptr, m_frames.at(index).path, nullptr, {});
            },
            static_cast<std::uint32_t>(m_frames.size()), an::ValidityRule::ZeroIsNoData,
            [this](const std::uint32_t index) { return m_frames.at(index).path; }, scope.engine().limits(),
            std::move(plan));
    }

    /// Every band of every frame, in bands of the stated heights from the top, held to the rows of the whole read.
    void expectBandsAreTheWholeReadsRows(WorkflowStackFrameSource& whole, WorkflowStackFrameSource& bands,
                                         const an::ExecutionContext& execution,
                                         const std::vector<std::uint32_t>& heights)
    {
        const an::Result<an::ImageDescriptor> described = whole.describe();
        ASSERT_TRUE(described.hasValue()) << described.status().message();
        const std::uint32_t channels = described->extent().channelCount();
        const std::size_t rowBytes = static_cast<std::size_t>(kWidth) * sizeof(float);
        for (std::uint32_t frame = 0u; frame < m_frames.size(); ++frame)
        {
            std::vector<std::byte> wholeSlot(rowBytes * kHeight * channels);
            const an::Result<an::StackSourceBand> wholeBand = whole.read(execution, frame, 0u, kHeight, wholeSlot);
            ASSERT_TRUE(wholeBand.hasValue()) << wholeBand.status().message();
            std::uint32_t firstRow = 0u;
            for (const std::uint32_t rows : heights)
            {
                std::vector<std::byte> slot(rowBytes * rows * channels);
                const an::Result<an::StackSourceBand> band = bands.read(execution, frame, firstRow, rows, slot);
                ASSERT_TRUE(band.hasValue()) << band.status().message();
                EXPECT_EQ(band->frameIndex, frame);
                EXPECT_EQ(band->firstRow, firstRow);
                EXPECT_EQ(band->sampleType, an::SampleType::Float32);
                ASSERT_NE(band->layout, nullptr);
                EXPECT_EQ(band->layout->extent().height(), rows);
                EXPECT_EQ(band->validity.descriptor.rule(), an::ValidityRule::ZeroIsNoData);
                for (std::uint32_t plane = 0u; plane < channels; ++plane)
                {
                    for (std::uint32_t y = 0u; y < rows; ++y)
                    {
                        const auto* expected = static_cast<const std::byte*>(wholeBand->samples) +
                                               *wholeBand->layout->rowByteOffset(plane, firstRow + y);
                        const auto* actual =
                            static_cast<const std::byte*>(band->samples) + *band->layout->rowByteOffset(plane, y);
                        EXPECT_EQ(std::memcmp(expected, actual, rowBytes), 0)
                            << "frame " << frame << " plane " << plane << " row " << firstRow + y;
                    }
                }
                firstRow += rows;
            }
            ASSERT_EQ(firstRow, kHeight);
        }
    }

    QTemporaryDir m_root;
    CalibrationRun m_run;
    std::vector<StackFrameRead> m_frames;
};

[[nodiscard]] std::vector<an::tests::fixtures::FixtureSampleOverride>
notANumberAt(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> pixels, const std::uint32_t channels = 1u)
{
    std::vector<an::tests::fixtures::FixtureSampleOverride> overrides;
    for (const auto& [x, y] : pixels)
    {
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            overrides.push_back(
                {.x = x, .y = y, .channel = channel, .value = std::numeric_limits<double>::quiet_NaN()});
        }
    }
    return overrides;
}

} // namespace

// Files read as they are - Float32, in planes, no mosaic - are read a band at a time, and every band is the rows of the
// whole read, in mono and colour: the same samples, a sample that is not a number read as the zero that means nothing
// on this route, the same description and the first file's metadata as the whole read states it.
TEST_F(StackFrameSourcesTest, BandsOfFilesReadAsTheyAreAreTheRowsOfTheWholeRead)
{
    for (const auto channels : {an::tests::fixtures::FixtureChannels::Mono, an::tests::fixtures::FixtureChannels::Rgb})
    {
        const std::uint32_t planes = channels == an::tests::fixtures::FixtureChannels::Rgb ? 3u : 1u;
        const QString tag = planes == 3u ? QStringLiteral("rgb") : QStringLiteral("mono");
        plan({writeFrame(tag + QStringLiteral("_a"), 11u, channels),
              writeFrame(tag + QStringLiteral("_b"), 12u, channels, an::tests::fixtures::FixtureSampleType::Float32,
                         notANumberAt({{0u, 0u}, {17u, 9u}, {39u, 29u}}, planes)),
              writeFrame(tag + QStringLiteral("_c"), 13u, channels)});
        an::Result<an::consumer::OperationScope> scope = admit();
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const auto whole = source(*scope, false);
        const auto bands = source(*scope, true);

        const an::Result<an::ImageDescriptor> wholeDescribed = whole->describe();
        const an::Result<an::ImageDescriptor> bandDescribed = bands->describe();
        ASSERT_TRUE(wholeDescribed.hasValue()) << wholeDescribed.status().message();
        ASSERT_TRUE(bandDescribed.hasValue()) << bandDescribed.status().message();
        EXPECT_EQ(*bandDescribed, *wholeDescribed);
        EXPECT_FALSE(whole->readsBands());
        EXPECT_EQ(whole->access(), an::StackSourceAccess::WholeFrame);
        EXPECT_TRUE(bands->readsBands()) << bands->wholeFrameReason().toStdString();
        EXPECT_TRUE(bands->wholeFrameReason().isEmpty());
        EXPECT_EQ(bands->access(), an::StackSourceAccess::ConcurrentRegion);
        ASSERT_NE(bands->firstMetadata(), nullptr);
        ASSERT_NE(whole->firstMetadata(), nullptr);
        EXPECT_TRUE(*bands->firstMetadata() == *whole->firstMetadata());

        expectBandsAreTheWholeReadsRows(*whole, *bands, scope->execution(), {7u, 16u, 7u});
        EXPECT_EQ(bands->bandsRead(), 9u);
        EXPECT_EQ(bands->framesRead(), 0u);

        // The samples the second file states as not a number are read as an exact zero, and no band holds one.
        std::vector<std::byte> slot(static_cast<std::size_t>(kWidth) * kHeight * planes * sizeof(float));
        const an::Result<an::StackSourceBand> band = bands->read(scope->execution(), 1u, 0u, kHeight, slot);
        ASSERT_TRUE(band.hasValue()) << band.status().message();
        const auto* samples = static_cast<const float*>(band->samples);
        for (std::size_t index = 0u; index < static_cast<std::size_t>(kWidth) * kHeight * planes; ++index)
        {
            ASSERT_TRUE(std::isfinite(samples[index])) << index;
        }
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
        {
            const std::size_t base = static_cast<std::size_t>(plane) * kWidth * kHeight;
            EXPECT_EQ(samples[base + 0u], 0.0f);
            EXPECT_EQ(samples[base + 9u * kWidth + 17u], 0.0f);
            EXPECT_EQ(samples[base + 29u * kWidth + 39u], 0.0f);
        }
    }
}

// A frame the bands cannot read keeps the stack on whole frames, for every frame, and the source says which and why:
// one stored in another representation, which the whole read widens, and a mosaic, which it demosaics.
TEST_F(StackFrameSourcesTest, AFrameTheBandsCannotReadKeepsTheWholeFrameForm)
{
    struct Case
    {
        const char* name;
        an::tests::fixtures::FixtureSampleType sampleType;
        an::tests::fixtures::FixtureCfa cfa;
        const char* reason;
    };
    for (const Case& odd : {Case{"codes", an::tests::fixtures::FixtureSampleType::UInt16,
                                 an::tests::fixtures::FixtureCfa::None, "is not stored as Float32"},
                            Case{"mosaic", an::tests::fixtures::FixtureSampleType::Float32,
                                 an::tests::fixtures::FixtureCfa::Rggb, "is a mosaic"}})
    {
        const QString name = QString::fromLatin1(odd.name);
        plan({writeFrame(name + QStringLiteral("_first"), 21u),
              writeFrame(name, 22u, an::tests::fixtures::FixtureChannels::Mono, odd.sampleType, {}, odd.cfa)});
        an::Result<an::consumer::OperationScope> scope = admit();
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const auto bands = source(*scope, true);
        // The whole read of a mosaic among plain frames refuses it by size; what matters here is the form chosen.
        const an::Result<an::ImageDescriptor> described = bands->describe();
        EXPECT_FALSE(bands->readsBands()) << odd.name;
        EXPECT_EQ(bands->access(), an::StackSourceAccess::WholeFrame);
        EXPECT_TRUE(bands->wholeFrameReason().contains(name + QStringLiteral(".fits")))
            << bands->wholeFrameReason().toStdString();
        EXPECT_TRUE(bands->wholeFrameReason().contains(QString::fromLatin1(odd.reason)))
            << bands->wholeFrameReason().toStdString();
        if (described.hasValue())
        {
            EXPECT_EQ(described->sampleType(), an::SampleType::Float32);
        }
    }
}

// A payload too short for the rows a band asks for fails that band, naming the frame; the bands it still holds are
// read; and the stack over the source fails with that frame named in what it returns.
TEST_F(StackFrameSourcesTest, AShortPayloadFailsTheBandItNoLongerHoldsNamingTheFrame)
{
    const QString shortened = writeFrame(QStringLiteral("shortened"), 32u);
    plan({writeFrame(QStringLiteral("whole_a"), 31u), shortened, writeFrame(QStringLiteral("whole_b"), 33u)});
    // The header stays and the payload loses its last ten rows; nothing holds the file to its revision.
    {
        QFile file(shortened);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.resize(file.size() - static_cast<qint64>(10u * kWidth * sizeof(float))));
    }
    m_frames[1].revision.reset();
    an::Result<an::consumer::OperationScope> scope = admit();
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const auto bands = source(*scope, true);
    ASSERT_TRUE(bands->describe().hasValue());
    ASSERT_TRUE(bands->readsBands()) << bands->wholeFrameReason().toStdString();

    std::vector<std::byte> slot(static_cast<std::size_t>(kWidth) * 10u * sizeof(float));
    EXPECT_TRUE(bands->read(scope->execution(), 1u, 0u, 10u, slot).hasValue());
    const an::Result<an::StackSourceBand> refused = bands->read(scope->execution(), 1u, 20u, 10u, slot);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_NE(refused.status().code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(std::string(refused.status().message()).find("shortened.fits") != std::string::npos)
        << refused.status().message();

    // The stack reads every frame's bands and stops at the one the payload no longer holds.
    const auto stacked = source(*scope, true);
    const an::Result<an::ImageDescriptor> described = stacked->describe();
    ASSERT_TRUE(described.hasValue()) << described.status().message();
    const std::optional<an::StackModel> model = an::StackModel::create(an::StackRequest{
        .input = {.frameCount = 3u, .descriptor = *described}, .combination = an::StackCombination::Average});
    ASSERT_TRUE(model.has_value());
    const an::Result<an::StackResult> result =
        an::executeStack(scope->engine(), scope->execution(), *model, *stacked,
                         an::StackSourceContract{.state = an::StackFrameState::Calibrated}, an::StackExecutionRequest{},
                         an::StackExecutionOutputs{});
    ASSERT_FALSE(result.hasValue());
    bool named = false;
    for (an::Status status = result.status(); !status.ok() && !named; status = status.cause())
    {
        named = std::string(status.message()).find("shortened.fits") != std::string::npos;
    }
    EXPECT_TRUE(named) << result.status().message();
}

// A stop requested between two bands is seen at the next band, which reads nothing.
TEST_F(StackFrameSourcesTest, AStopBetweenBandsIsSeenAtTheNextBand)
{
    plan({writeFrame(QStringLiteral("a"), 41u), writeFrame(QStringLiteral("b"), 42u)});
    std::stop_source stop;
    an::Result<an::consumer::OperationScope> scope = admit(stop.get_token());
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const auto bands = source(*scope, true);
    ASSERT_TRUE(bands->describe().hasValue());
    ASSERT_TRUE(bands->readsBands());
    std::vector<std::byte> slot(static_cast<std::size_t>(kWidth) * 10u * sizeof(float));
    ASSERT_TRUE(bands->read(scope->execution(), 0u, 0u, 10u, slot).hasValue());
    stop.request_stop();
    const an::Result<an::StackSourceBand> next = bands->read(scope->execution(), 1u, 0u, 10u, slot);
    ASSERT_FALSE(next.hasValue());
    EXPECT_EQ(next.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(bands->bandsRead(), 1u);
}

// A file that changed while the stack read it is refused by name when its last band is read, as the whole read refuses
// it after reading it whole.
TEST_F(StackFrameSourcesTest, AFileChangedWhileTheStackReadsItIsRefusedByName)
{
    const QString changed = writeFrame(QStringLiteral("changed"), 52u);
    plan({writeFrame(QStringLiteral("kept"), 51u), changed});
    an::Result<an::consumer::OperationScope> scope = admit();
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const auto bands = source(*scope, true);
    ASSERT_TRUE(bands->describe().hasValue());
    ASSERT_TRUE(bands->readsBands());
    std::vector<std::byte> slot(static_cast<std::size_t>(kWidth) * 15u * sizeof(float));
    ASSERT_TRUE(bands->read(scope->execution(), 1u, 0u, 15u, slot).hasValue());
    {
        QFile file(changed);
        ASSERT_TRUE(file.open(QIODevice::Append));
        file.write(QByteArray(2880, ' '));
    }
    const an::Result<an::StackSourceBand> refused = bands->read(scope->execution(), 1u, 15u, 15u, slot);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(std::string(refused.status().message()).find("changed.fits") != std::string::npos)
        << refused.status().message();
}

// The frames past the readers held open read each band through a reader opened for it, and the bands are the same.
TEST_F(StackFrameSourcesTest, FramesPastTheReadersHeldOpenReadEachBandThroughOneOfTheirOwn)
{
    plan({writeFrame(QStringLiteral("a"), 61u), writeFrame(QStringLiteral("b"), 62u),
          writeFrame(QStringLiteral("c"), 63u)});
    an::Result<an::consumer::OperationScope> scope = admit();
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const auto whole = source(*scope, false);
    const auto bands = source(*scope, true, 1u);
    ASSERT_TRUE(bands->describe().hasValue());
    ASSERT_TRUE(bands->readsBands());
    expectBandsAreTheWholeReadsRows(*whole, *bands, scope->execution(), {10u, 10u, 10u});
    EXPECT_EQ(bands->bandsRead(), 9u);
}

// Compressed files are read by bands like the others, every band the rows of the whole read, and the source states how
// many rows one piece of theirs spans: a subblock of 1 KiB holds 26 rows of the 40-byte rows of one byte's stretch of a
// shuffled Float32 frame 40 pixels wide. Files stored as they are read state one row.
TEST_F(StackFrameSourcesTest, CompressedFilesStateTheRowsOneOfTheirPiecesSpans)
{
    plan({compressedCopy(writeFrame(QStringLiteral("a"), 71u), QStringLiteral("a_zstd"), 1024u),
          compressedCopy(writeFrame(QStringLiteral("b"), 72u), QStringLiteral("b_zstd"), 1024u)});
    an::Result<an::consumer::OperationScope> scope = admit();
    ASSERT_TRUE(scope.hasValue()) << scope.status().message();
    const auto whole = source(*scope, false);
    const auto bands = source(*scope, true);
    ASSERT_TRUE(bands->describe().hasValue());
    ASSERT_TRUE(bands->readsBands()) << bands->wholeFrameReason().toStdString();
    EXPECT_EQ(bands->decodeGranularityRows(), 26u);
    expectBandsAreTheWholeReadsRows(*whole, *bands, scope->execution(), {10u, 10u, 10u});

    plan({writeFrame(QStringLiteral("c"), 73u), writeFrame(QStringLiteral("d"), 74u)});
    const auto stored = source(*scope, true);
    ASSERT_TRUE(stored->describe().hasValue());
    ASSERT_TRUE(stored->readsBands());
    EXPECT_EQ(stored->decodeGranularityRows(), 1u);
}

// A tile of a stack whose source reads bands holds at most 16 MiB of every frame's band, at least a row per worker and
// at most 1024 rows: 15 rows of 32 of the final stack's 8288-pixel lights at 8 workers, a row per worker where the
// workers outnumber that, and 1024 rows of small frames. A source decoding pieces of more rows - the 507 rows a 4 MiB
// subblock holds of those lights shuffled - may have a tile of them, past the 1024 rows too.
TEST(StackBandTileRowsTest, ATileHoldsSixteenMebibytesOfEveryFramesBandAndARowPerWorker)
{
    EXPECT_EQ(stage_utils::stackBandTileRows(32u, 8288u, 1u, 8u), 15u);
    EXPECT_EQ(stage_utils::stackBandTileRows(32u, 8288u, 3u, 8u), 8u);
    EXPECT_EQ(stage_utils::stackBandTileRows(72u, 8288u, 1u, 64u), 64u);
    EXPECT_EQ(stage_utils::stackBandTileRows(2u, 40u, 1u, 2u), 1024u);
    EXPECT_EQ(stage_utils::stackBandTileRows(0u, 40u, 1u, 0u), 1024u);
    EXPECT_EQ(stage_utils::stackBandTileRows(4096u, 8288u, 3u, 0u), 1u);
    EXPECT_EQ(stage_utils::stackBandTileRows(72u, 8288u, 1u, 32u, 507u), 507u);
    EXPECT_EQ(stage_utils::stackBandTileRows(32u, 8288u, 1u, 8u, 1u), 15u);
    EXPECT_EQ(stage_utils::stackBandTileRows(2u, 40u, 1u, 2u, 2000u), 2000u);
}
