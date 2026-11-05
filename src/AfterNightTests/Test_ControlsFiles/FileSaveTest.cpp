// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The file menu's save: the image written on a worker through the output transaction in the representation the export
/// settings' intent states, converted to it when the image's own is not one the container holds; a mosaic refused for a
/// container that cannot state its filter array before anything is written; each sample narrowed to the nearest code;
/// the view's pending changes cleared only when it still shows what was written; a view closed while its save runs
/// never touched; and a sequence written through the sequence companions.

#include "Test_ControlsFiles/FilesTestContext.h"

#include "controls/ImageViewFiles.h"
#include "controls/SequenceContainers.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Image.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <vector>

namespace
{

using an::tests::files::Channels;
using an::tests::files::FileManagerFixture;
using an::tests::files::FilesTestContext;
using an::tests::files::InstalledContext;
using an::tests::files::makeAsset;
using an::tests::files::openFilesContext;
using an::tests::files::pumpQuietly;
using an::tests::files::pumpUntil;

class FileSaveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openFilesContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_files = std::make_unique<FileManagerFixture>();
    }

    void TearDown() override
    {
        m_files.reset();
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    /// Shows a document in a view of the case's own and lets the view settle.
    template <typename SampleT>
    ImageViewControl& show(Channels channels,
                           const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
                           bool mosaic = false)
    {
        const std::optional<an::CfaDescriptor> cfa =
            mosaic ? an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u) : std::optional<an::CfaDescriptor>{};
        std::optional<an::consumer::ImageAsset> asset =
            makeAsset<SampleT>(m_context->engineSession(), 24u, 16u, channels, value,
                               an::NumericDomain::NormalizedLinear, cfa.value_or(an::CfaDescriptor{}));
        EXPECT_TRUE(asset.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("shown")});
        EXPECT_TRUE(document.hasValue());
        m_view = an::tests::viewer::makeView("shown");
        m_view->setDocument(std::move(*document));
        m_view->initializeNewImageHistory("made by the case");
        m_view->markPendingChanges();
        EXPECT_TRUE(an::tests::viewer::settle(*m_view));
        return *m_view;
    }

    /// Reads a file back through the product's registry; fails the case when it cannot.
    an::Result<an::CodecDecodeResult> readBack(const QString& path)
    {
        an::Result<an::consumer::SessionHold> hold = m_context->engineSession().hold();
        EXPECT_TRUE(hold.hasValue());
        an::Result<an::consumer::OperationScope> scope = m_context->engineSession().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        EXPECT_TRUE(scope.hasValue());
        const std::string file = path.toStdString();
        return an::decodeImage(scope->engine(), scope->execution(), m_context->registry(),
                               an::CodecReadRequest{.path = file});
    }

    /// The codes a grey file holds, read back through the view's own read, row by row; empty when it cannot be read.
    std::vector<double> codesIn(const QString& path)
    {
        std::vector<double> codes;
        an::Result<an::ui::ViewReadResult> read =
            an::ui::readViewImage(m_context->engineSession(), an::ui::ViewReadRequest{.path = path}, {}, nullptr);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue())
        {
            return codes;
        }
        an::Result<an::consumer::AssetReadLease> lease = read->asset.read();
        EXPECT_TRUE(lease.hasValue()) << lease.status().message();
        if (!lease.hasValue())
        {
            return codes;
        }
        lease->visit([&codes](const auto& typed) {
            const auto view = typed.view();
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!view.has_value() || !described.has_value())
            {
                return;
            }
            for (std::uint32_t y = 0u; y < described->extent().height(); ++y)
            {
                const auto row = view->row(0u, y);
                for (std::uint32_t x = 0u; row.has_value() && x < described->extent().width(); ++x)
                {
                    codes.push_back(static_cast<double>((*row)[x]));
                }
            }
        });
        return codes;
    }

    std::unique_ptr<FilesTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::unique_ptr<FileManagerFixture> m_files;
    an::tests::viewer::ViewHandle m_view;
};

} // namespace

// A Float32 image saved as PNG is written as sixteen-bit samples converted at corresponding full scales - the nearest
// code to each fraction of full scale - and the view is told it was saved.
TEST_F(FileSaveTest, AnImageIsConvertedToTheRepresentationTheContainerHolds)
{
    ImageViewControl& view = show<float>(Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return static_cast<float>(x + 24u * y) / 400.0f + 0.05f * static_cast<float>(channel);
    });
    ASSERT_TRUE(view.hasPendingChanges());
    const QString path = m_files->path(QStringLiteral("converted.png"));
    m_files->manager().saveViewToFile(view, path);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_EQ(m_files->saves.front().written, 1u);
    EXPECT_FALSE(m_files->saves.front().failed);
    EXPECT_TRUE(m_files->saves.front().viewMarkedSaved);
    EXPECT_FALSE(view.hasPendingChanges());
    EXPECT_EQ(view.document().names().sourcePath, path);

    an::Result<an::CodecDecodeResult> read = readBack(path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const std::optional<an::RawFrameDescriptor> frame = read->frame.descriptor();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->residentSampleType(), an::SampleType::UInt16);
    EXPECT_EQ(frame->extent().channelCount(), 3u);
    // The recent list and the file it names follow the save.
    ASSERT_FALSE(m_files->recentFiles.empty());
    EXPECT_TRUE(QString::fromStdString(m_files->recentFiles.front()).endsWith(QStringLiteral("converted.png")));
}

// A JPEG holds eight bits; a TIFF holds the image's own representation, with the compression the settings choose.
TEST_F(FileSaveTest, EachContainerIsWrittenAsTheSettingsSay)
{
    ImageViewControl& view = show<std::uint16_t>(Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::uint16_t>(1000u + 97u * x + 31u * y);
    });
    m_context->upsert(QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION), QStringLiteral("zip"));
    const QString jpeg = m_files->path(QStringLiteral("eight.jpg"));
    const QString tiff = m_files->path(QStringLiteral("same.tiff"));
    m_files->manager().saveViewToFile(view, jpeg);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    m_files->manager().saveViewToFile(view, tiff);
    ASSERT_TRUE(m_files->waitForSaves(2u));
    an::Result<an::CodecDecodeResult> eight = readBack(jpeg);
    ASSERT_TRUE(eight.hasValue()) << eight.status().message();
    EXPECT_EQ(eight->frame.descriptor()->residentSampleType(), an::SampleType::UInt8);
    an::Result<an::CodecDecodeResult> same = readBack(tiff);
    ASSERT_TRUE(same.hasValue()) << same.status().message();
    EXPECT_EQ(same->frame.descriptor()->residentSampleType(), an::SampleType::UInt16);
}

// An XISF file holds the image's own representation in the form the settings choose: Zstandard, its sixteen-bit
// samples shuffled as PixInsight writes them, in subblocks, with a SHA-256 checksum - and reads back as it was.
TEST_F(FileSaveTest, AnXisfFileIsWrittenInTheFormTheSettingsChoose)
{
    ImageViewControl& view = show<std::uint16_t>(Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<std::uint16_t>(1000u + 97u * x + 31u * y);
    });
    m_context->upsert(QString::fromLatin1(SettingsKeys::EXPORT_XISF_COMPRESSION), QStringLiteral("zstd"));
    m_context->upsert(QString::fromLatin1(SettingsKeys::EXPORT_XISF_CHECKSUM), QStringLiteral("sha256"));
    const QString path = m_files->path(QStringLiteral("formed.xisf"));
    m_files->manager().saveViewToFile(view, path);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_FALSE(m_files->saves.front().failed);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray header = file.read(16384);
    EXPECT_TRUE(header.contains("compression=\"zstd+sh:")) << header.left(2048).toStdString();
    EXPECT_TRUE(header.contains("checksum=\"sha256:")) << header.left(2048).toStdString();
    file.close();
    an::Result<an::CodecDecodeResult> read = readBack(path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->frame.descriptor()->residentSampleType(), an::SampleType::UInt16);
    const std::vector<double> codes = codesIn(path);
    ASSERT_GE(codes.size(), 2u);
    EXPECT_EQ(codes[0], 1000.0);
    EXPECT_EQ(codes[1], 1097.0);
}

// A mosaic is refused for a PNG before anything is written: the reason names the filter array, nothing is left at the
// path and the view keeps its pending changes. As FITS it is written whole, a mosaic still.
TEST_F(FileSaveTest, AMosaicIsRefusedForAPngBeforeAnythingIsWritten)
{
    ImageViewControl& view = show<std::uint16_t>(
        Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<std::uint16_t>(x * y); }, true);
    const QString png = m_files->path(QStringLiteral("mosaic.png"));
    m_files->manager().saveViewToFile(view, png);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_TRUE(m_files->saves.front().failed);
    EXPECT_EQ(m_files->saves.front().written, 0u);
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("error:File save error")));
    EXPECT_FALSE(QFileInfo::exists(png));
    EXPECT_EQ(QDir(m_files->path(QString())).entryList(QStringList{QStringLiteral("*mosaic*")}).size(), 0);
    EXPECT_TRUE(view.hasPendingChanges());

    const QString fits = m_files->path(QStringLiteral("mosaic.fits"));
    m_files->manager().saveViewToFile(view, fits);
    ASSERT_TRUE(m_files->waitForSaves(2u));
    EXPECT_FALSE(m_files->saves.back().failed);
    an::Result<an::CodecDecodeResult> read = readBack(fits);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_TRUE(read->frame.descriptor()->cfa().isMosaiced());
}

// A save narrows each sample to the nearest code at corresponding full scales - the rule every process, a container's
// frame and the command-line tool narrow by - and never truncates it to the code below: a Float32 image saved as a
// sixteen-bit PNG through the menu's save or as an eight-bit TIFF, and a sixteen-bit image saved as an eight-bit TIFF,
// read back as the nearest codes. Every sample lies a quarter or three quarters of a code past a whole one, so each odd
// code tells nearest from truncation.
TEST_F(FileSaveTest, ASaveNarrowsEachSampleToTheNearestCode)
{
    // The whole code a sample of a 24 by 16 image lies past, below a full scale of `codes`.
    const auto wholeCode = [](std::uint32_t x, std::uint32_t y, std::uint32_t codes) { return (x + 24u * y) % codes; };
    const auto between = [](std::uint32_t code) { return static_cast<double>(code) + (code % 2u != 0u ? 0.75 : 0.25); };
    const auto expectedCodes = [&wholeCode](std::uint32_t codes) {
        std::vector<double> expected;
        for (std::uint32_t y = 0u; y < 16u; ++y)
        {
            for (std::uint32_t x = 0u; x < 24u; ++x)
            {
                const std::uint32_t code = wholeCode(x, y, codes);
                expected.push_back(static_cast<double>(code + code % 2u));
            }
        }
        return expected;
    };

    ImageViewControl& view =
        show<float>(Channels::Grey, [&wholeCode, &between](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<float>(between(wholeCode(x, y, 65535u)) / 65535.0);
        });
    const QString png = m_files->path(QStringLiteral("nearest.png"));
    m_files->manager().saveViewToFile(view, png);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    ASSERT_FALSE(m_files->saves.front().failed);
    an::Result<an::CodecDecodeResult> wide = readBack(png);
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_EQ(wide->frame.descriptor()->residentSampleType(), an::SampleType::UInt16);
    EXPECT_EQ(codesIn(png), expectedCodes(65535u));

    an::Result<an::OutputIntent> eightBits =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt8});
    ASSERT_TRUE(eightBits.hasValue()) << eightBits.status().message();
    std::optional<an::consumer::ImageAsset> fractions =
        makeAsset<float>(m_context->engineSession(), 24u, 16u, Channels::Grey,
                         [&wholeCode, &between](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                             return static_cast<float>(between(wholeCode(x, y, 255u)) / 255.0);
                         });
    ASSERT_TRUE(fractions.has_value());
    const QString narrowed = m_files->path(QStringLiteral("nearest-8.tif"));
    const an::Status floatWritten =
        an::ui::writeViewImage(m_context->engineSession(), *fractions, narrowed, *eightBits, {});
    ASSERT_TRUE(floatWritten.ok()) << floatWritten.message();
    EXPECT_EQ(codesIn(narrowed), expectedCodes(255u));

    // Sixteen bits to eight: a code 257q + 200 lies 200/257 of an eight-bit code past q, and is written as q + 1.
    std::optional<an::consumer::ImageAsset> sixteen =
        makeAsset<std::uint16_t>(m_context->engineSession(), 24u, 16u, Channels::Grey,
                                 [&wholeCode](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                     return static_cast<std::uint16_t>(257u * wholeCode(x, y, 255u) + 200u);
                                 });
    ASSERT_TRUE(sixteen.has_value());
    const QString halved = m_files->path(QStringLiteral("nearest-16-to-8.tif"));
    const an::Status sixteenWritten =
        an::ui::writeViewImage(m_context->engineSession(), *sixteen, halved, *eightBits, {});
    ASSERT_TRUE(sixteenWritten.ok()) << sixteenWritten.message();
    std::vector<double> expected;
    for (std::uint32_t y = 0u; y < 16u; ++y)
    {
        for (std::uint32_t x = 0u; x < 24u; ++x)
        {
            expected.push_back(static_cast<double>(wholeCode(x, y, 255u) + 1u));
        }
    }
    EXPECT_EQ(codesIn(halved), expected);
}

// The writer of the view's own read function refuses a mosaic for a JPEG before converting anything, as the intent
// does: the view's own save cannot write one either.
TEST_F(FileSaveTest, TheViewsWriteRefusesAMosaicForAJpeg)
{
    std::optional<an::consumer::ImageAsset> mosaic = makeAsset<std::uint16_t>(
        m_context->engineSession(), 8u, 8u, Channels::Grey,
        [](std::uint32_t, std::uint32_t, std::uint32_t) { return 9u; }, an::NumericDomain::NormalizedLinear,
        *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u));
    ASSERT_TRUE(mosaic.has_value());
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        an::OutputIntentRequest{.sampleType = an::SampleType::UInt8, .compression = an::OutputCompression::Jpeg});
    ASSERT_TRUE(intent.hasValue());
    const QString path = m_files->path(QStringLiteral("refused.jpg"));
    const an::Status refused = an::ui::writeViewImage(m_context->engineSession(), *mosaic, path, *intent, {});
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::Unsupported);
    // Refused by the write itself, before any conversion, and saying what was refused: the codec would refuse only
    // after the work, and without naming the filter array.
    EXPECT_NE(std::string(refused.message()).find("filter array"), std::string::npos) << refused.message();
    EXPECT_FALSE(QFileInfo::exists(path));
}

// A write of the image after the save was asked for leaves the view's changes pending: what the view holds now is not
// what the save was asked to write. The save's read waits on its worker for the writer, never on the GUI thread.
TEST_F(FileSaveTest, AWriteMeanwhileKeepsTheChangesPending)
{
    ImageViewControl& view = show<float>(
        Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<float>(x) / 24.0f; });
    an::Result<an::ui::ImageDocumentWriter> writer = view.document().tryWrite();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    const QString path = m_files->path(QStringLiteral("before-write.fits"));
    m_files->manager().saveViewToFile(view, path);
    pumpQuietly();
    // The save waits for the writer on its worker; the GUI thread turned meanwhile.
    EXPECT_TRUE(m_files->saves.empty());
    an::tests::controls::writeSample(writer->asset(), 0u, 0u, 0.75f);
    writer->release();
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_EQ(m_files->saves.front().written, 1u);
    EXPECT_FALSE(m_files->saves.front().viewMarkedSaved);
    EXPECT_TRUE(view.hasPendingChanges());
    EXPECT_TRUE(QFileInfo::exists(path));
}

// A view closed while its save runs is never touched: the file is written whole from the image held when the save was
// asked for, and the completion finds no view to tell.
TEST_F(FileSaveTest, AViewClosedWhileItsSaveRunsIsNeverTouched)
{
    ImageViewControl& view = show<std::uint16_t>(Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<std::uint16_t>(100u * c + 3u * x + 7u * y);
    });
    const QString path = m_files->path(QStringLiteral("closed.xisf"));
    m_files->manager().saveViewToFile(view, path);
    m_view.reset();
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_EQ(m_files->saves.front().written, 1u);
    EXPECT_FALSE(m_files->saves.front().viewMarkedSaved);
    an::Result<an::CodecDecodeResult> read = readBack(path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->frame.descriptor()->extent().width(), 24u);
}

// A sequence view's save writes the sequence its document states through the sequence companions, every frame of that
// document's own sequence rather than the frame the view shows, and a write that cannot be published leaves nothing.
TEST_F(FileSaveTest, ASequenceIsWrittenFromTheDocumentsSequence)
{
    const QString source = m_files->path(QStringLiteral("source.ser"));
    an::tests::viewer::writeSerFile(m_context->engineSession(), source, 5u, 3u, 3u,
                                    [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
                                        return 0.2f * static_cast<float>(frame) + 0.03f * static_cast<float>(x + y);
                                    });
    m_files->manager().openFiles(QStringList{source}, QFileInfo(source).absolutePath());
    ASSERT_TRUE(m_files->waitForOpens(1u));
    ASSERT_EQ(m_context->madeViews().size(), 1u);
    ImageViewControl* view = m_context->madeViews().front();
    ASSERT_TRUE(view->isSequence());
    const QString copy = m_files->path(QStringLiteral("copy.ser"));
    m_files->manager().saveViewToFile(*view, copy);
    ASSERT_TRUE(m_files->waitForSaves(1u));
    EXPECT_EQ(m_files->saves.front().written, 1u);
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(m_context->engineSession());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::ui::OpenedSequence> written =
            an::ui::openSequenceSource(scope->engine(), scope->execution(), copy, an::SequenceFormat::Ser);
        ASSERT_TRUE(written.hasValue()) << written.status().message();
        EXPECT_EQ(written->descriptor.frameCount, std::optional<std::uint64_t>(3u));
        ASSERT_TRUE(written->descriptor.fixedFrame.has_value());
        EXPECT_EQ(written->descriptor.fixedFrame->extent().width(), 5u);
        EXPECT_EQ(written->descriptor.fixedFrame->extent().height(), 3u);
    }

    const QString refused = m_files->path(QStringLiteral("absent/refused.ser"));
    m_files->manager().saveViewToFile(*view, refused);
    ASSERT_TRUE(m_files->waitForSaves(2u));
    EXPECT_TRUE(m_files->saves.back().failed);
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("error:File save error")));
    EXPECT_FALSE(QFileInfo::exists(refused));
}
