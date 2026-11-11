// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's bounded previews: the run history's thumbnail and the report's master and map
/// previews. An image is shrunk to the bound by the largest sample under each preview pixel, into Float32 fractions of
/// full scale, and presented as the viewer presents an image - each colour channel its own automatic screen transfer -
/// so a preview of an image within the bound is the viewer's presentation of that image, and one of a larger image is
/// the viewer's presentation of the shrunk one. Only one- and three-channel images are previewed. The stack's own
/// preview of the master it holds is stretched the same way.

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ImageIntegration/ImportSession.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/workflow/PreviewThumbnailGenerator.h"
#include "processes/ImageIntegration/workflow/stages/StackOutputFinalization.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QRect>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <variant>

namespace
{

using an::tests::viewer::Channels;
using SampleRule = std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>;

/// A Float32 image of the session whose samples, fractions of full scale, the rule states.
std::optional<an::consumer::ImageAsset> makeImage(const an::consumer::EngineSession& session, std::uint32_t width,
                                                  std::uint32_t height, Channels channels, const SampleRule& rule)
{
    return an::tests::viewer::makeAsset<float>(session, width, height, channels, rule);
}

/// Writes an image as a FITS file in its own representation; fails the case when it cannot.
void writeFits(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image, const QString& path)
{
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    ASSERT_TRUE(described.has_value());
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = described->sampleType()});
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    const an::Status written = an::ui::writeViewImage(session, image, path, *intent, {});
    ASSERT_TRUE(written.ok()) << written.message();
}

/// The viewer's presentation of an image, each colour channel its own screen transfer unless `linked` asks for one
/// transfer for all of them, as the bytes a view paints.
QImage viewerPresentation(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
                          bool linked = false)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return {};
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return {};
    }
    an::ui::ViewPresentationSettings settings{};
    settings.request.linked = linked;
    an::Result<an::ui::ViewPresentationOutcome> presented =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, nullptr, &session.memory());
    EXPECT_TRUE(presented.hasValue()) << presented.status().message();
    if (!presented.hasValue() || !presented->image)
    {
        return {};
    }
    return an::ui::presentedQImage(presented->image).copy();
}

bool previewImagesMatch(const QImage& lhs, const QImage& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    const QImage lhsRgb = lhs.convertToFormat(QImage::Format_RGB888);
    const QImage rhsRgb = rhs.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < lhsRgb.height(); ++y)
    {
        for (int x = 0; x < lhsRgb.width(); ++x)
        {
            if (lhsRgb.pixelColor(x, y) != rhsRgb.pixelColor(x, y))
            {
                return false;
            }
        }
    }
    return true;
}

/// A sample of an image, or nothing when it holds no Float32 samples there.
std::optional<float> sampleAt(const an::consumer::ImageAsset& image, std::uint32_t x, std::uint32_t y,
                              std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    const auto* typed = std::get_if<an::Image<float>>(&lease->image());
    if (typed == nullptr)
    {
        return std::nullopt;
    }
    const std::optional<an::ImageView<const float>> view = typed->view();
    if (!view.has_value())
    {
        return std::nullopt;
    }
    const auto row = view->row(channel, y);
    if (!row.has_value())
    {
        return std::nullopt;
    }
    return (*row)[x];
}

/// A grey test map deliberately skewed so its automatic stretch does something.
float skewedMonoMap(std::uint32_t x, std::uint32_t y, std::uint32_t)
{
    float value = 0.001f;
    if ((x + y) % 11u == 0u)
    {
        value = 0.02f;
    }
    if ((x + y) % 23u == 0u)
    {
        value = 0.2f;
    }
    if ((x + y) % 41u == 0u)
    {
        value = 1.0f;
    }
    return value;
}

} // namespace

class PreviewThumbnailGenerator : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_temp.isValid());
        an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(session).value());
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return *m_session;
    }

    [[nodiscard]] QString tempPath() const
    {
        return m_temp.path();
    }

    QTemporaryDir m_temp;
    std::optional<an::consumer::EngineSession> m_session;
};

TEST_F(PreviewThumbnailGenerator, WritesPreviewThumbnail)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("stacked_test.fit"));
    const QString runDir = QDir(tempPath()).filePath(QStringLiteral("history/20250101_120000_000"));
    ASSERT_TRUE(QDir().mkpath(runDir));

    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), 64u, 64u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.05f + 0.001f * static_cast<float>((x + 2u * y) % 17u);
        });
    ASSERT_TRUE(image.has_value());
    writeFits(session(), *image, outputPath);

    const auto previewRel = ImageIntegration::generatePreviewThumbnail(session(), outputPath, runDir);
    ASSERT_TRUE(previewRel.has_value());
    EXPECT_EQ(previewRel.value(), QStringLiteral("preview.png"));

    const QString previewPath = QDir(runDir).filePath(previewRel.value());
    QImage preview(previewPath);
    EXPECT_FALSE(preview.isNull());
    EXPECT_LE(preview.width(), 256);
    EXPECT_LE(preview.height(), 256);
}

TEST_F(PreviewThumbnailGenerator, ResidentImagePreviewIsBoundedAndDoesNotMutateSource)
{
    constexpr std::uint32_t sourceWidth = 2048u;
    constexpr std::uint32_t sourceHeight = 1024u;
    constexpr QSize maximumSize(256, 256);

    const std::optional<an::consumer::ImageAsset> image = makeImage(
        session(), sourceWidth, sourceHeight, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<float>((x + 3u * y) % 257u) / 256.0f;
        });
    ASSERT_TRUE(image.has_value());

    const std::optional<float> sourceCorner = sampleAt(*image, 0u, 0u);
    const std::optional<float> sourceCenter = sampleAt(*image, sourceWidth / 2u, sourceHeight / 2u);
    ASSERT_TRUE(sourceCorner.has_value());
    ASSERT_TRUE(sourceCenter.has_value());

    ImageIntegration::PreviewGenerationDiagnostics diagnostics;
    std::optional<QImage> preview;
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        preview = ImageIntegration::generateBoundedPreview(session(), lease->image(), maximumSize, &diagnostics);
    }

    ASSERT_TRUE(preview.has_value());
    EXPECT_FALSE(preview->isNull());
    EXPECT_EQ(preview->size(), QSize(256, 128));
    EXPECT_LE(preview->width(), maximumSize.width());
    EXPECT_LE(preview->height(), maximumSize.height());
    EXPECT_EQ(diagnostics.sourceSize, QSize(static_cast<int>(sourceWidth), static_cast<int>(sourceHeight)));
    EXPECT_EQ(diagnostics.previewSize, preview->size());
    EXPECT_EQ(diagnostics.sourcePixelCount, static_cast<std::size_t>(sourceWidth) * sourceHeight);
    EXPECT_EQ(diagnostics.workingImagePixelCount, static_cast<std::size_t>(preview->width() * preview->height()));
    EXPECT_EQ(diagnostics.sourceSizedIntermediatePixelCount, 0u);
    EXPECT_EQ(diagnostics.stretchStatisticsPixelCount, diagnostics.workingImagePixelCount);
    EXPECT_EQ(diagnostics.interleavedDisplayBufferBytes, diagnostics.workingImagePixelCount);
    EXPECT_LT(diagnostics.workingImagePixelCount, diagnostics.sourcePixelCount);
    EXPECT_EQ(sampleAt(*image, 0u, 0u), sourceCorner);
    EXPECT_EQ(sampleAt(*image, sourceWidth / 2u, sourceHeight / 2u), sourceCenter);
}

TEST_F(PreviewThumbnailGenerator, StrongDownsamplePreservesAnIsolatedStar)
{
    constexpr std::uint32_t sourceWidth = 1024u;
    constexpr std::uint32_t sourceHeight = 1024u;
    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), sourceWidth, sourceHeight, Channels::Grey,
                  [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return x == 1u && y == 1u ? 1.0f : 0.0f; });
    ASSERT_TRUE(image.has_value());

    std::optional<QImage> preview;
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        preview = ImageIntegration::generateBoundedPreview(session(), lease->image(), QSize(16, 16));
    }

    ASSERT_TRUE(preview.has_value());
    ASSERT_FALSE(preview->isNull());
    EXPECT_EQ(preview->size(), QSize(16, 16));
    int brightestPixel = 0;
    for (int y = 0; y < preview->height(); ++y)
    {
        for (int x = 0; x < preview->width(); ++x)
        {
            brightestPixel = (std::max)(brightestPixel, preview->pixelColor(x, y).red());
        }
    }
    EXPECT_GT(brightestPixel, 0);
}

TEST_F(PreviewThumbnailGenerator, ReportPreviewUsesBoundedDefaultWhenCallerOmitsMaximum)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("wide_rejection_map.fit"));
    const QString reportDir = QDir(tempPath()).filePath(QStringLiteral("report_previews"));
    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), 2300u, 100u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.01f * static_cast<float>((x + y) % 7u);
        });
    ASSERT_TRUE(image.has_value());
    writeFits(session(), *image, outputPath);

    const auto previewName =
        ImageIntegration::generateReportPreviewPng(session(), outputPath, reportDir, QStringLiteral("bounded.png"));
    ASSERT_TRUE(previewName.has_value());

    const QImage preview(QDir(reportDir).filePath(*previewName));
    ASSERT_FALSE(preview.isNull());
    EXPECT_LE(preview.width(), 2048);
    EXPECT_LE(preview.height(), 2048);
}

TEST_F(PreviewThumbnailGenerator, ResidentRgbPreviewUsesUnlinkedViewerStretch)
{
    constexpr std::uint32_t width = 96u;
    constexpr std::uint32_t height = 48u;
    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), width, height, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            constexpr float channelBase[3] = {0.01f, 0.08f, 0.32f};
            constexpr float channelHighlight[3] = {0.24f, 0.16f, 0.45f};
            float value = channelBase[channel] + 0.001f * static_cast<float>((x * 5u + y * 7u + channel) % 11u);
            if ((x + y + channel * 3u) % 31u == 0u)
            {
                value += channelHighlight[channel];
            }
            return value;
        });
    ASSERT_TRUE(image.has_value());

    ImageIntegration::PreviewGenerationDiagnostics diagnostics;
    std::optional<QImage> preview;
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        preview = ImageIntegration::generateBoundedPreview(session(), lease->image(), QSize(128, 128), &diagnostics);
    }

    ASSERT_TRUE(preview.has_value());
    EXPECT_FALSE(preview->isNull());
    EXPECT_EQ(preview->size(), QSize(static_cast<int>(width), static_cast<int>(height)));
    EXPECT_TRUE(diagnostics.usedUnlinkedRgbStretch);
    EXPECT_EQ(diagnostics.interleavedDisplayBufferBytes, static_cast<std::size_t>(width) * height * 3u);

    // The image is within the bound, so its preview is the viewer's presentation of the image itself.
    const QImage expectedUnlinked = viewerPresentation(session(), *image, false);
    const QImage expectedLinked = viewerPresentation(session(), *image, true);
    ASSERT_FALSE(expectedUnlinked.isNull());
    ASSERT_FALSE(expectedLinked.isNull());
    EXPECT_TRUE(previewImagesMatch(*preview, expectedUnlinked));
    EXPECT_FALSE(previewImagesMatch(*preview, expectedLinked));
}

TEST_F(PreviewThumbnailGenerator, RejectsNullEmptyImagesAndInvalidBounds)
{
    EXPECT_FALSE(
        ImageIntegration::generateBoundedPreview(session(), an::consumer::AnyImage{}, QSize(64, 64)).has_value());

    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), 8u, 8u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(image.has_value());
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    EXPECT_FALSE(ImageIntegration::generateBoundedPreview(session(), lease->image(), QSize()).has_value());
    EXPECT_FALSE(ImageIntegration::generateBoundedPreview(session(), lease->image(), QSize(0, 8)).has_value());
}

TEST_F(PreviewThumbnailGenerator, RejectsUnsupportedInterleavedChannelLayouts)
{
    // Only one- and three-channel images are previewed; an image with an alpha channel is refused.
    const std::optional<an::consumer::ImageAsset> fourChannels =
        makeImage(session(), 8u, 8u, Channels::Rgba, [](std::uint32_t, std::uint32_t, std::uint32_t channel) {
            return 0.1f * static_cast<float>(channel + 1u);
        });
    ASSERT_TRUE(fourChannels.has_value());
    an::Result<an::consumer::AssetReadLease> lease = fourChannels->read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    EXPECT_FALSE(ImageIntegration::generateBoundedPreview(session(), lease->image(), QSize(8, 8)).has_value());
}

TEST_F(PreviewThumbnailGenerator, WritesAutostretchedReportPreviewPng)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("rejection_map.fit"));
    const QString reportDir = QDir(tempPath()).filePath(QStringLiteral("report_previews/GroupA"));
    ASSERT_TRUE(QDir().mkpath(reportDir));

    const std::optional<an::consumer::ImageAsset> image = makeImage(session(), 64u, 64u, Channels::Grey, skewedMonoMap);
    ASSERT_TRUE(image.has_value());
    writeFits(session(), *image, outputPath);

    const auto previewRel = ImageIntegration::generateReportPreviewPng(session(), outputPath, reportDir,
                                                                       QStringLiteral("rejection_map_1.png"));
    ASSERT_TRUE(previewRel.has_value());
    EXPECT_EQ(previewRel.value(), QStringLiteral("rejection_map_1.png"));

    const QString previewPath = QDir(reportDir).filePath(previewRel.value());
    QImage preview(previewPath);
    EXPECT_FALSE(preview.isNull());
    EXPECT_EQ(preview.width(), 64);
    EXPECT_EQ(preview.height(), 64);
}

TEST_F(PreviewThumbnailGenerator, ReportPreviewPngMatchesViewerAutostretchForMono)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("rejection_map.fit"));
    const QString reportDir = QDir(tempPath()).filePath(QStringLiteral("report_previews/GroupA"));
    ASSERT_TRUE(QDir().mkpath(reportDir));

    // Deliberately skewed mono map to exercise the automatic stretch.
    const std::optional<an::consumer::ImageAsset> image = makeImage(session(), 64u, 64u, Channels::Grey, skewedMonoMap);
    ASSERT_TRUE(image.has_value());
    writeFits(session(), *image, outputPath);

    const auto previewRel = ImageIntegration::generateReportPreviewPng(session(), outputPath, reportDir,
                                                                       QStringLiteral("rejection_map_1.png"));
    ASSERT_TRUE(previewRel.has_value());

    const QString previewPath = QDir(reportDir).filePath(previewRel.value());
    const QImage generated(previewPath);
    ASSERT_FALSE(generated.isNull());

    // The map is within the bound, so its preview is the viewer's presentation of the map itself.
    const QImage expected = viewerPresentation(session(), *image);
    ASSERT_FALSE(expected.isNull());
    ASSERT_EQ(generated.size(), expected.size());

    const QImage generatedGrey = generated.convertToFormat(QImage::Format_Grayscale8);
    const QImage expectedGrey = expected.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < generatedGrey.height(); ++y)
    {
        const uchar* gRow = generatedGrey.constScanLine(y);
        const uchar* eRow = expectedGrey.constScanLine(y);
        for (int x = 0; x < generatedGrey.width(); ++x)
        {
            EXPECT_EQ(gRow[x], eRow[x]) << "x=" << x << " y=" << y;
        }
    }
}

TEST_F(PreviewThumbnailGenerator, ColorReportAndHistoryPreviewsUseUnlinkedAutostretch)
{
    const QString outputPath = QDir(tempPath()).filePath(QStringLiteral("stacked_rgb.fit"));
    const QString runDir = QDir(tempPath()).filePath(QStringLiteral("history/20250101_120000_000"));
    const QString reportDir = QDir(tempPath()).filePath(QStringLiteral("report_previews/GroupA"));
    ASSERT_TRUE(QDir().mkpath(runDir));
    ASSERT_TRUE(QDir().mkpath(reportDir));

    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), 64u, 64u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            constexpr float channelBase[3] = {0.015f, 0.08f, 0.23f};
            constexpr float channelHighlights[3] = {0.22f, 0.14f, 0.28f};
            float value = channelBase[channel] + 0.001f * static_cast<float>((x * 3u + y * 5u + channel) % 7u);
            if ((x + y + channel * 3u) % 29u == 0u)
            {
                value += channelHighlights[channel];
            }
            return value;
        });
    ASSERT_TRUE(image.has_value());
    writeFits(session(), *image, outputPath);

    const QImage expectedUnlinked = viewerPresentation(session(), *image, false);
    const QImage expectedLinked = viewerPresentation(session(), *image, true);
    ASSERT_FALSE(expectedUnlinked.isNull());
    ASSERT_FALSE(expectedLinked.isNull());
    ASSERT_FALSE(previewImagesMatch(expectedUnlinked, expectedLinked));

    const auto reportPreviewRel =
        ImageIntegration::generateReportPreviewPng(session(), outputPath, reportDir, QStringLiteral("master.png"));
    ASSERT_TRUE(reportPreviewRel.has_value());
    const QImage reportPreview(QDir(reportDir).filePath(reportPreviewRel.value()));
    ASSERT_FALSE(reportPreview.isNull());
    EXPECT_TRUE(previewImagesMatch(reportPreview, expectedUnlinked));
    EXPECT_FALSE(previewImagesMatch(reportPreview, expectedLinked));

    const auto historyPreviewRel =
        ImageIntegration::generatePreviewThumbnail(session(), outputPath, runDir, QSize(64, 64));
    ASSERT_TRUE(historyPreviewRel.has_value());
    const QImage historyPreview(QDir(runDir).filePath(historyPreviewRel.value()));
    ASSERT_FALSE(historyPreview.isNull());
    EXPECT_TRUE(previewImagesMatch(historyPreview, expectedUnlinked));
    EXPECT_FALSE(previewImagesMatch(historyPreview, expectedLinked));
}

// The stack's preview of a colour master - the thumbnail beside it the integration window shows, and the report's
// picture - is stretched as the run history's and the report's previews are: each colour channel on its own.
TEST_F(PreviewThumbnailGenerator, ResidentStackPreviewOfAColourMasterUsesUnlinkedAutostretch)
{
    // As wide as the window's thumbnail, so the thumbnail is the presentation itself rather than a rescaling of it.
    constexpr std::uint32_t width = 256u;
    constexpr std::uint32_t height = 128u;
    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), width, height, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            constexpr float channelBase[3] = {0.02f, 0.07f, 0.26f};
            constexpr float channelHighlights[3] = {0.25f, 0.12f, 0.31f};
            float value = channelBase[channel] + 0.001f * static_cast<float>((x * 3u + y * 7u + channel) % 9u);
            if ((x + 2u * y + channel * 5u) % 37u == 0u)
            {
                value += channelHighlights[channel];
            }
            return value;
        });
    ASSERT_TRUE(image.has_value());

    const QImage expectedUnlinked = viewerPresentation(session(), *image, false);
    const QImage expectedLinked = viewerPresentation(session(), *image, true);
    ASSERT_FALSE(expectedUnlinked.isNull());
    ASSERT_FALSE(expectedLinked.isNull());
    ASSERT_FALSE(previewImagesMatch(expectedUnlinked, expectedLinked));

    const QString thumbnailPath = QDir(tempPath()).filePath(QStringLiteral("master.fit.preview.png"));
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const auto* master = std::get_if<an::Image<float>>(&lease->image());
        ASSERT_NE(master, nullptr);
        an::Result<an::consumer::OperationScope> scope = session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const StackPreviewAssets assets =
            ImageIntegration::detail::generateResidentStackPreviewAssets(*scope, *master, thumbnailPath);
        EXPECT_FALSE(assets.reportPreviewJpeg.isEmpty());
        EXPECT_EQ(assets.reportPreviewSize, QSize(static_cast<int>(width), static_cast<int>(height)));
    }

    const QImage thumbnail(thumbnailPath);
    ASSERT_FALSE(thumbnail.isNull());
    EXPECT_TRUE(previewImagesMatch(thumbnail, expectedUnlinked));
    EXPECT_FALSE(previewImagesMatch(thumbnail, expectedLinked));
}

TEST_F(PreviewThumbnailGenerator, PreviewOfALargerImageIsTheViewerPresentationOfItsShrunkImage)
{
    // Every two-by-two block of the source holds one bright sample and three dim ones; the preview at half the size
    // keeps each block's brightest, which is the image a quarter of the size holding those brightest samples.
    constexpr std::uint32_t width = 64u;
    constexpr std::uint32_t height = 48u;
    const auto blockPeak = [](std::uint32_t blockX, std::uint32_t blockY) {
        return 0.05f + 0.01f * static_cast<float>((blockX * 3u + blockY * 5u) % 13u);
    };
    const std::optional<an::consumer::ImageAsset> image =
        makeImage(session(), width, height, Channels::Grey, [&](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            const float peak = blockPeak(x / 2u, y / 2u);
            return (x % 2u == 1u && y % 2u == 0u) ? peak : 0.25f * peak;
        });
    ASSERT_TRUE(image.has_value());
    const std::optional<an::consumer::ImageAsset> shrunk =
        makeImage(session(), width / 2u, height / 2u, Channels::Grey,
                  [&](std::uint32_t x, std::uint32_t y, std::uint32_t) { return blockPeak(x, y); });
    ASSERT_TRUE(shrunk.has_value());

    std::optional<QImage> preview;
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        preview = ImageIntegration::generateBoundedPreview(session(), lease->image(),
                                                           QSize(static_cast<int>(width / 2u), 1000));
    }
    ASSERT_TRUE(preview.has_value());
    ASSERT_EQ(preview->size(), QSize(static_cast<int>(width / 2u), static_cast<int>(height / 2u)));

    const QImage expected = viewerPresentation(session(), *shrunk);
    ASSERT_FALSE(expected.isNull());
    EXPECT_TRUE(previewImagesMatch(*preview, expected));
}

// ---- The detail: the centre of a master at full scale, stretched as its whole-frame preview is.

TEST(ReportDetailRegion, IsAtMostNineSixtyByFiveFortyAtTheCentreOfTheImage)
{
    EXPECT_EQ(ImageIntegration::reportDetailRegion(QSize(4000, 3000)), QRect(1520, 1230, 960, 540));
    EXPECT_EQ(ImageIntegration::reportDetailRegion(QSize(960, 540)), QRect(0, 0, 960, 540));
    // A master smaller than the region is its own detail; one smaller one way is whole that way.
    EXPECT_EQ(ImageIntegration::reportDetailRegion(QSize(64, 48)), QRect(0, 0, 64, 48));
    EXPECT_EQ(ImageIntegration::reportDetailRegion(QSize(2000, 300)), QRect(520, 0, 960, 300));
    EXPECT_EQ(ImageIntegration::reportDetailRegion(QSize(500, 2000)), QRect(0, 730, 500, 540));
    EXPECT_TRUE(ImageIntegration::reportDetailRegion(QSize(0, 0)).isEmpty());
    EXPECT_TRUE(ImageIntegration::reportDetailRegion(QSize()).isEmpty());
}

TEST_F(PreviewThumbnailGenerator, ResidentStackPreviewKeepsADetailStretchedAsTheWholeFrameIs)
{
    const auto detailOf = [this](const std::uint32_t width, const std::uint32_t height, StackPreviewAssets& assets) {
        const std::optional<an::consumer::ImageAsset> image =
            makeImage(session(), width, height, Channels::Grey, skewedMonoMap);
        ASSERT_TRUE(image.has_value());
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const auto* master = std::get_if<an::Image<float>>(&lease->image());
        ASSERT_NE(master, nullptr);
        an::Result<an::consumer::OperationScope> scope = session().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        assets = ImageIntegration::detail::generateResidentStackPreviewAssets(
            *scope, *master, QDir(tempPath()).filePath(QStringLiteral("detail_%1.preview.png").arg(width)));
    };

    // A master no larger than the region: the detail is the whole frame, the same pixels through the same stretch.
    StackPreviewAssets small;
    detailOf(200u, 120u, small);
    ASSERT_FALSE(small.reportPreviewJpeg.isEmpty());
    EXPECT_EQ(small.reportDetailSize, QSize(200, 120));
    EXPECT_EQ(small.reportDetailJpeg, small.reportPreviewJpeg);

    // A larger one: 960 by 540 of its centre, one picture pixel for each of the master's.
    StackPreviewAssets large;
    detailOf(2400u, 1300u, large);
    ASSERT_FALSE(large.reportPreviewJpeg.isEmpty());
    EXPECT_EQ(large.reportPreviewSize, QSize(1200, 650));
    EXPECT_EQ(large.reportDetailSize, QSize(960, 540));
    EXPECT_EQ(QImage::fromData(large.reportDetailJpeg, "JPEG").size(), QSize(960, 540));
}

TEST_F(PreviewThumbnailGenerator, OneReadOfAMastersFileWritesItsWholeFrameAndItsDetail)
{
    const QString reportDir = QDir(tempPath()).filePath(QStringLiteral("report_previews/GroupA"));

    // A small master is its own detail: the two files hold the same picture.
    const QString smallPath = QDir(tempPath()).filePath(QStringLiteral("small_master.fit"));
    const std::optional<an::consumer::ImageAsset> small = makeImage(session(), 64u, 48u, Channels::Grey, skewedMonoMap);
    ASSERT_TRUE(small.has_value());
    writeFits(session(), *small, smallPath);
    const ImageIntegration::ReportMasterPreviews smallWritten = ImageIntegration::generateReportMasterPreviews(
        session(), smallPath, reportDir, QStringLiteral("master.jpg"), QStringLiteral("detail.jpg"));
    ASSERT_TRUE(smallWritten.whole.has_value());
    ASSERT_TRUE(smallWritten.detail.has_value());
    const QImage smallWhole(QDir(reportDir).filePath(*smallWritten.whole));
    const QImage smallDetail(QDir(reportDir).filePath(*smallWritten.detail));
    ASSERT_EQ(smallWhole.size(), QSize(64, 48));
    ASSERT_EQ(smallDetail.size(), QSize(64, 48));
    EXPECT_TRUE(previewImagesMatch(smallDetail, smallWhole)) << "the detail is stretched as the whole frame is";

    // A large one: the whole frame within its bound, the detail 960 by 540.
    const QString largeDir = QDir(tempPath()).filePath(QStringLiteral("report_previews/GroupB"));
    const QString largePath = QDir(tempPath()).filePath(QStringLiteral("large_master.fit"));
    const std::optional<an::consumer::ImageAsset> large =
        makeImage(session(), 2400u, 1300u, Channels::Grey, skewedMonoMap);
    ASSERT_TRUE(large.has_value());
    writeFits(session(), *large, largePath);
    const ImageIntegration::ReportMasterPreviews largeWritten = ImageIntegration::generateReportMasterPreviews(
        session(), largePath, largeDir, QStringLiteral("master.jpg"), QStringLiteral("detail.jpg"));
    ASSERT_TRUE(largeWritten.whole.has_value());
    ASSERT_TRUE(largeWritten.detail.has_value());
    EXPECT_EQ(QImage(QDir(largeDir).filePath(QStringLiteral("master.jpg"))).size(), QSize(2048, 1109));
    EXPECT_EQ(QImage(QDir(largeDir).filePath(QStringLiteral("detail.jpg"))).size(), QSize(960, 540));

    // A file that does not read gives neither.
    const ImageIntegration::ReportMasterPreviews none = ImageIntegration::generateReportMasterPreviews(
        session(), QDir(tempPath()).filePath(QStringLiteral("absent.fit")), reportDir, QStringLiteral("none.jpg"),
        QStringLiteral("none_detail.jpg"));
    EXPECT_FALSE(none.whole.has_value());
    EXPECT_FALSE(none.detail.has_value());
    EXPECT_FALSE(QFile::exists(QDir(reportDir).filePath(QStringLiteral("none_detail.jpg"))));
}
