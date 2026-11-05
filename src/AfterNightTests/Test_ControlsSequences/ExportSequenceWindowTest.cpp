// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Export Sequence window and its export: a path given its container's extension; a sequence exported to SER, AVI
/// and video frame for frame; each frame stretched as Apply Stretch stretches it; a resized frame fitted, filled,
/// stretched or centred; a mosaic demosaiced when asked; an export that is stopped or cannot be written leaving
/// nothing; and the window exporting what its controls ask for and remembering its choices.

#include "Test_ControlsSequences/SequencesTestSupport.h"

#include "controls/SequenceContainers.h"
#include "controls/AutostretchApply.h"
#include "controls/ExportSequenceControl.h"
#include "widgets/ImageViewWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <stop_token>

namespace
{

using namespace an::tests::sequences;

/// A SER fixture written in the case's directory, and the document sequence that states it.
struct SourceSequence
{
    FixtureSequence fixture;
    an::ui::DocumentSequence sequence;
};

[[nodiscard]] SourceSequence writeSource(SequencesTestContext& context, const FixtureSequence& fixture,
                                         const QString& name = QStringLiteral("capture.ser"))
{
    SourceSequence source{.fixture = fixture};
    source.sequence.sourcePath = context.pathOf(name);
    source.sequence.descriptor.format = an::SequenceFormat::Ser;
    source.sequence.descriptor.frameCount = fixture.frames;
    EXPECT_TRUE(writeFixture(context.engineSession(), source.sequence.sourcePath, an::SequenceFormat::Ser, fixture));
    return source;
}

/// A window, deleted with the case once its export has landed.
struct WindowDeleter
{
    void operator()(ExportSequenceControl* window) const
    {
        if (window == nullptr)
        {
            return;
        }
        (void)pumpUntil([window]() { return !window->isExportRunning(); });
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        delete window;
    }
};
using WindowHandle = std::unique_ptr<ExportSequenceControl, WindowDeleter>;

[[nodiscard]] WindowHandle openWindow(const an::ui::DocumentSequence& sequence, std::uint32_t width,
                                      std::uint32_t height, bool mosaic)
{
    WindowHandle window(new ExportSequenceControl(nullptr, sequence, width, height, mosaic, AutostretchParams{}));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    return window;
}

/// The samples of frame `index` of a read-back, plane `channel` only.
[[nodiscard]] std::vector<double> planeOf(const ReadBack& read, std::size_t index, std::uint32_t channel)
{
    const an::ImageDescriptor described = *read.frames[index].toImageDescriptor();
    const std::size_t plane = static_cast<std::size_t>(described.extent().width()) * described.extent().height();
    const std::vector<double>& all = read.samples[index];
    return std::vector<double>(all.begin() + static_cast<std::ptrdiff_t>(plane * channel),
                               all.begin() + static_cast<std::ptrdiff_t>(plane * (channel + 1u)));
}

} // namespace

// An export's path takes its container's extension: kept when it already ends in it, replacing another, appended
// otherwise; a VP9 video is WebM.
TEST(ExportSequenceWindowTest, AnExportPathTakesItsContainersExtension)
{
    EXPECT_EQ(an::ui::normalizeSequenceExportPath(QStringLiteral("out/run"), QStringLiteral("ser")),
              QStringLiteral("out/run.ser"));
    EXPECT_EQ(an::ui::normalizeSequenceExportPath(QStringLiteral(" out/run.avi "), QStringLiteral(".mp4")),
              QStringLiteral("out/run.mp4"));
    EXPECT_EQ(an::ui::normalizeSequenceExportPath(QStringLiteral("out.d/run"), QStringLiteral("ser")),
              QStringLiteral("out.d/run.ser"));
    EXPECT_EQ(an::ui::normalizeSequenceExportPath(QStringLiteral("run.SER"), QStringLiteral("ser")),
              QStringLiteral("run.SER"));
    EXPECT_TRUE(an::ui::normalizeSequenceExportPath(QString(), QStringLiteral("ser")).isEmpty());
    EXPECT_EQ(an::ui::sequenceExportSuffix(an::ui::SequenceExportFormat::Ser, an::ui::SequenceExportCodec::H264),
              QStringLiteral("ser"));
    EXPECT_EQ(an::ui::sequenceExportSuffix(an::ui::SequenceExportFormat::RawAvi, an::ui::SequenceExportCodec::H264),
              QStringLiteral("avi"));
    EXPECT_EQ(an::ui::sequenceExportSuffix(an::ui::SequenceExportFormat::Video, an::ui::SequenceExportCodec::H265),
              QStringLiteral("mp4"));
    EXPECT_EQ(an::ui::sequenceExportSuffix(an::ui::SequenceExportFormat::Video, an::ui::SequenceExportCodec::Vp9),
              QStringLiteral("webm"));
}

// A sequence exports to SER frame for frame, to AVI at eight bits at corresponding full scales, and to a video holding
// every frame at its size, at the frame rate asked.
TEST(ExportSequenceWindowTest, ASequenceExportsToSerAviAndVideo)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const SourceSequence source = writeSource(*context, FixtureSequence{.frames = 3u});

    an::ui::ExportSequenceRequest request;
    request.sequence = source.sequence;
    request.fps = 12.0;
    request.outputPath = context->pathOf(QStringLiteral("out.ser"));
    {
        const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> ser = readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Ser);
    ASSERT_TRUE(ser.has_value());
    ASSERT_EQ(ser->samples.size(), 3u);
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        EXPECT_EQ(largestDifference(ser->samples[frame], fixtureSamples(frame, source.fixture, false)), 0.0);
    }

    request.format = an::ui::SequenceExportFormat::RawAvi;
    request.outputPath = context->pathOf(QStringLiteral("out.avi"));
    {
        const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> avi = readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Avi);
    ASSERT_TRUE(avi.has_value());
    ASSERT_EQ(avi->samples.size(), 3u);
    ASSERT_TRUE(avi->descriptor.nominalFrameRate.has_value());
    EXPECT_DOUBLE_EQ(an::ui::framesPerSecondOf(*avi->descriptor.nominalFrameRate), 12.0);
    const std::vector<double> wide = fixtureSamples(1u, source.fixture, false);
    const std::vector<double> narrow = planeOf(*avi, 1u, 0u);
    ASSERT_EQ(narrow.size(), wide.size());
    for (std::size_t i = 0u; i < wide.size(); ++i)
    {
        EXPECT_EQ(narrow[i], std::round(wide[i] * 255.0 / 65535.0)) << "sample " << i;
    }

    // The H.265 encoder opens on frames of a camera's size, not on the 8 by 6 fixture.
    if (an::isFfmpegEncoderAvailable(an::FfmpegVideoCodec::H265))
    {
        const SourceSequence large = writeSource(*context, FixtureSequence{.frames = 3u, .width = 128u, .height = 96u},
                                                 QStringLiteral("large.ser"));
        request.sequence = large.sequence;
        request.format = an::ui::SequenceExportFormat::Video;
        request.codec = an::ui::SequenceExportCodec::H265;
        request.outputPath = context->pathOf(QStringLiteral("out.mp4"));
        {
            const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
            ASSERT_TRUE(checked.ok()) << checked.message();
        }
        const std::optional<ReadBack> video =
            readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Mp4);
        ASSERT_TRUE(video.has_value());
        ASSERT_EQ(video->frames.size(), 3u);
        EXPECT_EQ(video->frames[2].toImageDescriptor()->extent().width(), 128u);
        EXPECT_EQ(video->frames[2].toImageDescriptor()->extent().height(), 96u);
        // The coding asked for is the coding written.
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
        ASSERT_TRUE(scope.has_value());
        const std::string written = request.outputPath.toStdString();
        an::Result<an::FfmpegStreamInfo> stream =
            an::inspectFfmpegFile(scope->engine(), scope->execution(), written,
                                  an::FfmpegSourceOptions{.unspecifiedMatrix = an::FfmpegMatrixAssumption::Bt601,
                                                          .unspecifiedRange = an::FfmpegRangeAssumption::Limited});
        ASSERT_TRUE(stream.hasValue()) << stream.status().message();
        EXPECT_EQ(stream->codecName, "hevc");
    }
}

// Each frame of an export is stretched as Apply Stretch stretches it, with the view's settings.
TEST(ExportSequenceWindowTest, EachFrameIsStretchedAsApplyStretchStretchesIt)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const SourceSequence source = writeSource(*context, FixtureSequence{.frames = 2u, .width = 16u, .height = 12u});

    an::ui::ExportSequenceRequest request;
    request.sequence = source.sequence;
    request.autostretch = true;
    request.stretch.targetBackground = 0.3;
    request.stretch.linkedChannels = true;
    request.outputPath = context->pathOf(QStringLiteral("stretched.ser"));
    {
        const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> read =
        readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Ser);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->samples.size(), 2u);

    const std::optional<ReadBack> original =
        readBack(context->engineSession(), source.sequence.sourcePath, an::SequenceFormat::Ser);
    ASSERT_TRUE(original.has_value());
    for (std::uint32_t frame = 0u; frame < 2u; ++frame)
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint16_t>(
            context->engineSession(), 16u, 12u, Channels::Grey,
            [frame](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return fixtureSample(frame, x, y, c); });
        ASSERT_TRUE(asset.has_value());
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        ASSERT_TRUE(lease.hasValue());
        an::Result<an::consumer::ImageAsset> stretched =
            an::ui::applyAutostretchToCopy(context->engineSession(), lease->image(), nullptr, request.stretch, {});
        ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();
        EXPECT_EQ(largestDifference(read->samples[frame], samplesOf(*stretched)), 0.0) << "frame " << frame;
        EXPECT_GT(largestDifference(read->samples[frame], original->samples[frame]), 0.0);
    }
}

// A resized frame is fitted inside the size with black borders, made to fill it and cropped about its centre,
// stretched to it, or centred unscaled - padded where it is smaller and cropped where it is larger.
TEST(ExportSequenceWindowTest, AResizedFrameIsFittedFilledStretchedOrCentred)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const SourceSequence source = writeSource(*context, FixtureSequence{.frames = 2u, .width = 8u, .height = 6u});
    const auto exported = [&](an::ui::SequenceExportFill fill, std::uint32_t width, std::uint32_t height,
                              const QString& name) -> std::vector<double> {
        an::ui::ExportSequenceRequest request;
        request.sequence = source.sequence;
        request.resize = true;
        request.width = width;
        request.height = height;
        request.fillMode = fill;
        request.outputPath = context->pathOf(name);
        const an::Status done = an::ui::exportSequence(context->engineSession(), request, {});
        EXPECT_TRUE(done.ok()) << done.message();
        const std::optional<ReadBack> read =
            readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Ser);
        EXPECT_TRUE(read.has_value());
        if (!read.has_value() || read->frames.size() != 2u)
        {
            return {};
        }
        EXPECT_EQ(read->frames[1].toImageDescriptor()->extent().width(), width);
        EXPECT_EQ(read->frames[1].toImageDescriptor()->extent().height(), height);
        return read->samples[1];
    };
    // Frame 1's samples are all at least 397, so a zero is a border.
    const auto zeroAt = [](const std::vector<double>& samples, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
        return samples[static_cast<std::size_t>(y) * width + x] == 0.0;
    };

    const std::vector<double> fit = exported(an::ui::SequenceExportFill::Fit, 16u, 6u, QStringLiteral("fit.ser"));
    ASSERT_EQ(fit.size(), 96u);
    EXPECT_TRUE(zeroAt(fit, 16u, 0u, 3u));
    EXPECT_TRUE(zeroAt(fit, 16u, 3u, 0u));
    EXPECT_FALSE(zeroAt(fit, 16u, 4u, 0u));
    EXPECT_FALSE(zeroAt(fit, 16u, 11u, 5u));
    EXPECT_TRUE(zeroAt(fit, 16u, 12u, 5u));

    const std::vector<double> fill = exported(an::ui::SequenceExportFill::Fill, 16u, 6u, QStringLiteral("fill.ser"));
    ASSERT_EQ(fill.size(), 96u);
    EXPECT_TRUE(std::none_of(fill.begin(), fill.end(), [](double sample) { return sample == 0.0; }));

    const std::vector<double> stretch =
        exported(an::ui::SequenceExportFill::Stretch, 16u, 6u, QStringLiteral("stretch.ser"));
    ASSERT_EQ(stretch.size(), 96u);
    EXPECT_TRUE(std::none_of(stretch.begin(), stretch.end(), [](double sample) { return sample == 0.0; }));

    const std::vector<double> centred =
        exported(an::ui::SequenceExportFill::Center, 16u, 6u, QStringLiteral("centre.ser"));
    ASSERT_EQ(centred.size(), 96u);
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 16u; ++x)
        {
            const double expected = x >= 4u && x < 12u ? static_cast<double>(fixtureSample(1u, x - 4u, y, 0u)) : 0.0;
            EXPECT_EQ(centred[static_cast<std::size_t>(y) * 16u + x], expected) << x << "," << y;
        }
    }

    const std::vector<double> cropped =
        exported(an::ui::SequenceExportFill::Center, 4u, 4u, QStringLiteral("cropped.ser"));
    ASSERT_EQ(cropped.size(), 16u);
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_EQ(cropped[static_cast<std::size_t>(y) * 4u + x],
                      static_cast<double>(fixtureSample(1u, x + 2u, y + 1u, 0u)));
        }
    }
}

// A mosaic is demosaiced when the export asks, and written to AVI as its grey samples otherwise.
TEST(ExportSequenceWindowTest, AMosaicIsDemosaicedWhenAsked)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    SourceSequence source = writeSource(*context, FixtureSequence{.frames = 2u, .cfa = *rggb});
    source.sequence.demosaic = an::DemosaicMethod::Bilinear;

    an::ui::ExportSequenceRequest request;
    request.sequence = source.sequence;
    request.outputPath = context->pathOf(QStringLiteral("colour.ser"));
    request.debayerCfa = true;
    {
        const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> colour =
        readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Ser);
    ASSERT_TRUE(colour.has_value());
    EXPECT_EQ(colour->frames[0].toImageDescriptor()->channelSchema(), an::ChannelSchema::Rgb);

    request.debayerCfa = false;
    request.outputPath = context->pathOf(QStringLiteral("mosaic.ser"));
    {
        const an::Status checked = an::ui::exportSequence(context->engineSession(), request, {});
        ASSERT_TRUE(checked.ok()) << checked.message();
    }
    const std::optional<ReadBack> mosaic =
        readBack(context->engineSession(), request.outputPath, an::SequenceFormat::Ser);
    ASSERT_TRUE(mosaic.has_value());
    EXPECT_TRUE(mosaic->frames[0].toImageDescriptor()->isMosaiced());
}

// An export that is stopped, or whose file cannot be written, leaves nothing at its path.
TEST(ExportSequenceWindowTest, AnExportThatCannotCompleteLeavesNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const SourceSequence source = writeSource(*context, FixtureSequence{.frames = 3u});
    an::ui::ExportSequenceRequest request;
    request.sequence = source.sequence;
    request.outputPath = context->pathOf(QStringLiteral("stopped.avi"));
    request.format = an::ui::SequenceExportFormat::RawAvi;
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::ui::exportSequence(context->engineSession(), request, stop.get_token()).code(),
              an::ErrorCode::Cancelled);
    request.outputPath = context->pathOf(QStringLiteral("no-such-directory/out.avi"));
    EXPECT_TRUE(an::ui::exportSequence(context->engineSession(), request, {}).failed());
    request.outputPath.clear();
    EXPECT_EQ(an::ui::exportSequence(context->engineSession(), request, {}).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(filesIn(context->directory()), QStringList{QStringLiteral("capture.ser")});
}

// An export that fails part way - a SER whose frames change size - leaves nothing at its path, the frames it wrote
// included.
TEST(ExportSequenceWindowTest, AnExportThatFailsPartWayLeavesNothing)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    an::ImageListManifest manifest;
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const std::uint32_t size = frame == 2u ? 4u : 8u;
        const QString still = context->pathOf(QStringLiteral("still-%1.fits").arg(frame));
        ASSERT_TRUE(writeFitsStill(context->engineSession(), still, frame, size, size, Channels::Grey));
        manifest.framePaths.push_back(QFileInfo(still).fileName().toStdString());
    }
    an::ui::DocumentSequence list;
    list.sourcePath = context->pathOf(QStringLiteral("list.anseq"));
    list.descriptor.format = an::SequenceFormat::ImageList;
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
        ASSERT_TRUE(scope.has_value());
        const std::string destination = list.sourcePath.toStdString();
        ASSERT_TRUE(an::writeImageListManifest(scope->engine(), scope->execution(),
                                               an::SequenceOutputRequest{.destinationPath = destination}, manifest)
                        .hasValue());
    }
    const QStringList before = filesIn(context->directory());
    an::ui::ExportSequenceRequest request;
    request.sequence = list;
    request.outputPath = context->pathOf(QStringLiteral("changing.ser"));
    const an::Status exported = an::ui::exportSequence(context->engineSession(), request, {});
    EXPECT_TRUE(exported.failed());
    EXPECT_EQ(filesIn(context->directory()), before);
}

// The window exports what its controls ask for on a worker, reports it, and remembers its format, coding and resolution
// for the next window.
TEST(ExportSequenceWindowTest, TheWindowExportsWhatItsControlsAskForAndRemembersItsChoices)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const SourceSequence source = writeSource(*context, FixtureSequence{.frames = 2u});
    WindowHandle window = openWindow(source.sequence, 8u, 6u, false);
    EXPECT_FALSE(window->ui().debayerCfaCheckBox->isVisibleTo(window.get()));
    window->ui().outputFormatSelector->setCurrentText(QStringLiteral("AVI (RAW)"));
    window->ui().outputFileName->setText(context->pathOf(QStringLiteral("exported")));
    const an::ui::ExportSequenceRequest request = window->buildExportRequest();
    EXPECT_EQ(request.format, an::ui::SequenceExportFormat::RawAvi);
    EXPECT_TRUE(request.outputPath.endsWith(QStringLiteral("exported.avi")));

    window->startExport();
    EXPECT_TRUE(window->isExportRunning());
    ASSERT_TRUE(pumpUntil([&]() { return !window->isExportRunning(); }));
    EXPECT_TRUE(context->messages.contains(QStringLiteral("info:Export Sequence")));
    EXPECT_TRUE(QFileInfo::exists(request.outputPath));
    ASSERT_TRUE(context->processSettings.contains(QStringLiteral("__EXPORT_SEQUENCE__")));
    EXPECT_TRUE(
        context->processSettings.value(QStringLiteral("__EXPORT_SEQUENCE__")).contains(QStringLiteral("AVI (RAW)")));

    WindowHandle next = openWindow(source.sequence, 8u, 6u, true);
    EXPECT_EQ(next->ui().outputFormatSelector->currentText(), QStringLiteral("AVI (RAW)"));
    EXPECT_TRUE(next->ui().debayerCfaCheckBox->isVisibleTo(next.get()));
}

// Preserve Ratio keeps the frame's proportions as a side changes; a preset size fixes both; Free Ratio starts again
// from the frame's own size.
TEST(ExportSequenceWindowTest, PreserveRatioKeepsTheFramesProportions)
{
    std::unique_ptr<SequencesTestContext> context = openSequencesContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    an::ui::DocumentSequence sequence;
    sequence.sourcePath = context->pathOf(QStringLiteral("capture.ser"));
    sequence.descriptor.format = an::SequenceFormat::Ser;
    WindowHandle window = openWindow(sequence, 80u, 60u, false);
    EXPECT_EQ(window->ui().widthValue->value(), 80);
    EXPECT_EQ(window->ui().heightValue->value(), 60);
    window->ui().widthValue->setValue(160);
    EXPECT_EQ(window->ui().heightValue->value(), 120);
    window->ui().resolutionSelector->setCurrentText(QStringLiteral("Full HD"));
    EXPECT_EQ(window->ui().widthValue->value(), 1920);
    EXPECT_EQ(window->ui().heightValue->value(), 1080);
    EXPECT_FALSE(window->ui().widthValue->isEnabled());
    window->ui().resolutionSelector->setCurrentText(QStringLiteral("Free Ratio"));
    EXPECT_EQ(window->ui().widthValue->value(), 80);
    EXPECT_EQ(window->ui().heightValue->value(), 60);
    EXPECT_TRUE(window->ui().widthValue->isEnabled());
}
