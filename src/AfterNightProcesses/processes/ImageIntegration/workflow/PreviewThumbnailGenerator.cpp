// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The integration's bounded previews over the consumer runtime and the viewer's presentation.

#include "stdafx.hpp"
#include "PreviewThumbnailGenerator.h"

#include "controls/ImageViewFiles.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFileInfo>
#include <QImageWriter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace ImageIntegration
{
namespace
{
constexpr QSize kDefaultMaximumReportPreviewSize(2048, 2048);

template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

[[nodiscard]] std::optional<QSize> previewSizeFor(const an::ImageDescriptor& descriptor, const QSize& maxSize)
{
    const std::uint32_t width = descriptor.extent().width();
    const std::uint32_t height = descriptor.extent().height();
    constexpr auto kLargest = static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (width == 0u || height == 0u || !maxSize.isValid() || maxSize.width() <= 0 || maxSize.height() <= 0 ||
        width > kLargest || height > kLargest)
    {
        return std::nullopt;
    }

    const QSize sourceSize(static_cast<int>(width), static_cast<int>(height));
    if (sourceSize.width() <= maxSize.width() && sourceSize.height() <= maxSize.height())
    {
        return sourceSize;
    }

    const double scale = (std::min)(static_cast<double>(maxSize.width()) / static_cast<double>(sourceSize.width()),
                                    static_cast<double>(maxSize.height()) / static_cast<double>(sourceSize.height()));
    const int previewWidth =
        (std::max)(1, (std::min)(maxSize.width(), static_cast<int>(std::floor(sourceSize.width() * scale))));
    const int previewHeight =
        (std::max)(1, (std::min)(maxSize.height(), static_cast<int>(std::floor(sourceSize.height() * scale))));
    return QSize(previewWidth, previewHeight);
}

/// A sample as a fraction of full scale: an unsigned integer over its largest value, a signed one counted from its
/// smallest value to its largest, a floating sample as it is.
template <typename SampleT> [[nodiscard]] float fractionOf(const SampleT sample)
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<float>(sample);
    }
    else if constexpr (std::is_unsigned_v<SampleT>)
    {
        return static_cast<float>(static_cast<double>(sample) /
                                  static_cast<double>((std::numeric_limits<SampleT>::max)()));
    }
    else
    {
        constexpr double low = static_cast<double>((std::numeric_limits<SampleT>::min)());
        constexpr double high = static_cast<double>((std::numeric_limits<SampleT>::max)());
        return static_cast<float>((static_cast<double>(sample) - low) / (high - low));
    }
}

/// The preview's description: the source's channels and colour as fractions of full scale at the preview's size. A
/// mosaic's filter array does not survive a reduction, so a mosaic is previewed as the grey samples it holds.
[[nodiscard]] std::optional<an::ImageDescriptor> previewDescriptor(const an::ImageDescriptor& source,
                                                                   const an::ImageExtent& extent)
{
    an::ImageDescriptorRequest request = source.toRequest();
    request.extent = extent;
    request.sampleType = an::SampleType::Float32;
    request.cfa = an::CfaDescriptor{};
    if (request.numericDomain != an::NumericDomain::NormalizedLinear &&
        request.numericDomain != an::NumericDomain::EncodedNonlinear)
    {
        request.numericDomain = an::NumericDomain::NormalizedLinear;
    }
    if (const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request))
    {
        return described;
    }
    // A description the reduction cannot keep - an unusual colour space for these channels - is previewed as plain
    // samples of the same count.
    request.colorSpace = source.colorSpace();
    return an::ImageDescriptor::create(request);
}

/// Shrinks an image into a Float32 preview, each preview sample the largest sample of its footprint, without an
/// intermediate the size of the source.
[[nodiscard]] an::Result<an::Image<float>> shrinkByLargestSample(const an::EngineContext& engine,
                                                                 const an::consumer::AnyImage& source,
                                                                 const an::ImageDescriptor& sourceDescriptor,
                                                                 const QSize& previewSize)
{
    const std::uint32_t channels = sourceDescriptor.extent().channelCount();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(static_cast<std::uint32_t>(previewSize.width()),
                                static_cast<std::uint32_t>(previewSize.height()), channels, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the preview's size is not an image's");
    }
    const std::optional<an::ImageDescriptor> descriptor = previewDescriptor(sourceDescriptor, *extent);
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the preview cannot describe the image");
    }
    an::Result<an::Image<float>> preview = an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                                                    an::StorageInitialization::Uninitialized);
    if (!preview.hasValue())
    {
        return preview.status();
    }
    std::optional<an::ImageWriteAccess<float>> written = preview->write();
    if (!written.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the preview holds nothing");
    }
    const an::ImageView<float>& to = written->view();

    const an::Status shrunk = std::visit(
        [&](const auto& typed) -> an::Status {
            using SampleT = SampleOf<decltype(typed)>;
            const std::optional<an::ImageView<const SampleT>> from = typed.view();
            if (!from.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
            }
            const std::uint32_t sourceWidth = from->width();
            const std::uint32_t sourceHeight = from->height();
            const std::uint32_t perPixel = from->samplesPerPixel();
            const auto targetWidth = static_cast<std::uint64_t>(previewSize.width());
            const auto targetHeight = static_cast<std::uint64_t>(previewSize.height());
            for (std::uint32_t channel = 0; channel < channels; ++channel)
            {
                const std::uint32_t plane = channel / perPixel;
                const std::uint32_t inPixel = channel % perPixel;
                for (std::uint64_t y = 0; y < targetHeight; ++y)
                {
                    const std::optional<std::span<float>> out = to.row(channel, static_cast<std::uint32_t>(y));
                    if (!out.has_value())
                    {
                        return an::Status::failure(an::ErrorCode::Internal, "the preview's row is not there");
                    }
                    const auto rowBegin = static_cast<std::uint32_t>(y * sourceHeight / targetHeight);
                    const auto rowEnd =
                        (std::max)(rowBegin + 1u, static_cast<std::uint32_t>((y + 1u) * sourceHeight / targetHeight));
                    std::fill(out->begin(), out->end(), -(std::numeric_limits<float>::max)());
                    for (std::uint32_t sourceY = rowBegin; sourceY < rowEnd; ++sourceY)
                    {
                        const std::optional<std::span<const SampleT>> in = from->row(plane, sourceY);
                        if (!in.has_value())
                        {
                            return an::Status::failure(an::ErrorCode::Internal, "the image's row is not there");
                        }
                        for (std::uint64_t x = 0; x < targetWidth; ++x)
                        {
                            const auto columnBegin = static_cast<std::uint32_t>(x * sourceWidth / targetWidth);
                            const auto columnEnd =
                                (std::max)(columnBegin + 1u,
                                           static_cast<std::uint32_t>((x + 1u) * sourceWidth / targetWidth));
                            float& peak = (*out)[static_cast<std::size_t>(x)];
                            for (std::uint32_t sourceX = columnBegin; sourceX < columnEnd; ++sourceX)
                            {
                                peak = (std::max)(peak,
                                                  fractionOf(
                                                      (*in)[static_cast<std::size_t>(sourceX) * perPixel + inPixel]));
                            }
                        }
                    }
                }
            }
            return an::Status::success();
        },
        source);
    written.reset();
    if (shrunk.failed())
    {
        return shrunk;
    }
    return preview;
}

/// A bounded preview and the balance and statistics it was presented through.
struct PresentedPreview
{
    QImage image;
    an::DisplayPresentationAnalysis analysis{};
};

/// The bounded preview of an image, as `generateBoundedPreview` states it, with what it was presented through.
[[nodiscard]] std::optional<PresentedPreview> boundedPreview(const an::consumer::EngineSession& session,
                                                             const an::consumer::AnyImage& image, const QSize& maxSize,
                                                             PreviewGenerationDiagnostics* diagnostics)
{
    if (diagnostics)
    {
        *diagnostics = {};
    }
    const std::optional<an::ImageDescriptor> descriptor = descriptorOf(image);
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    const auto previewSize = previewSizeFor(*descriptor, maxSize);
    const std::uint32_t channelCount = descriptor->extent().channelCount();
    if (!previewSize.has_value() || (channelCount != 1u && channelCount != 3u))
    {
        return std::nullopt;
    }

    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> shrunk = shrinkByLargestSample(scope->engine(), image, *descriptor, *previewSize);
    if (!shrunk.hasValue())
    {
        LOG_WARNING("PreviewThumbnailGenerator: no preview: {}", shrunk.status().message());
        return std::nullopt;
    }
    const std::size_t workingPixels =
        static_cast<std::size_t>(previewSize->width()) * static_cast<std::size_t>(previewSize->height());
    if (diagnostics)
    {
        diagnostics->sourceSize =
            QSize(static_cast<int>(descriptor->extent().width()), static_cast<int>(descriptor->extent().height()));
        diagnostics->previewSize = *previewSize;
        diagnostics->sourcePixelCount = static_cast<std::size_t>(descriptor->extent().pixelCount());
        diagnostics->workingImagePixelCount = workingPixels;
    }

    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*shrunk)});
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = asset->read(std::stop_token{});
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    // The viewer's presentation: each channel its own automatic screen transfer, into interleaved bytes.
    const an::ui::ViewPresentationSettings settings{};
    an::Result<an::ui::ViewPresentationOutcome> presented =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, nullptr, &session.memory());
    if (!presented.hasValue() || !presented->image)
    {
        LOG_WARNING("PreviewThumbnailGenerator: the preview was not presented: {}",
                    presented.hasValue() ? "nothing was presented" : presented.status().message());
        return std::nullopt;
    }
    PresentedPreview preview;
    preview.image = an::ui::presentedQImage(presented->image).copy();
    preview.analysis = presented->analysis;
    if (preview.image.isNull())
    {
        return std::nullopt;
    }
    if (diagnostics)
    {
        diagnostics->stretchStatisticsPixelCount = workingPixels;
        diagnostics->usedUnlinkedRgbStretch = channelCount == 3u && !settings.request.linked;
        diagnostics->interleavedDisplayBufferBytes = workingPixels * channelCount;
    }
    return preview;
}

/// The detail of an image under a read lease: the region at its centre at full scale, presented through the analysis
/// its whole-frame preview was presented through. An analysis the presentation refuses is set aside and the whole
/// image measured, as a view measures it.
[[nodiscard]] std::optional<QImage> detailPreview(const an::consumer::EngineSession& session,
                                                  const an::consumer::AssetReadLease& source,
                                                  const an::DisplayPresentationAnalysis& analysis)
{
    const std::optional<an::ImageDescriptor> descriptor = descriptorOf(source.image());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    constexpr auto kLargest = static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    const std::uint32_t width = descriptor->extent().width();
    const std::uint32_t height = descriptor->extent().height();
    const std::uint32_t channelCount = descriptor->extent().channelCount();
    if (width > kLargest || height > kLargest || (channelCount != 1u && channelCount != 3u))
    {
        return std::nullopt;
    }
    const QRect region = reportDetailRegion(QSize(static_cast<int>(width), static_cast<int>(height)));
    if (region.isEmpty())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    an::ui::ViewPresentationSettings settings{};
    settings.region = an::ui::ViewRegion{.x = static_cast<std::uint32_t>(region.x()),
                                         .y = static_cast<std::uint32_t>(region.y()),
                                         .width = static_cast<std::uint32_t>(region.width()),
                                         .height = static_cast<std::uint32_t>(region.height())};
    an::Result<an::ui::ViewPresentationOutcome> presented =
        an::ui::presentForView(scope->engine(), scope->execution(), source, settings, &analysis, &session.memory());
    if (!presented.hasValue() || !presented->image)
    {
        LOG_WARNING("PreviewThumbnailGenerator: the detail was not presented: {}",
                    presented.hasValue() ? "nothing was presented" : presented.status().message());
        return std::nullopt;
    }
    QImage detail = an::ui::presentedQImage(presented->image).copy();
    if (detail.isNull())
    {
        return std::nullopt;
    }
    return detail;
}

/// The pictures one read of a file gives.
struct FilePreviews
{
    std::optional<QImage> whole;
    std::optional<QImage> detail;
};

/// Reads a file once and previews it: the whole frame within `maxSize`, and its detail when asked.
[[nodiscard]] FilePreviews previewsFromPath(const an::consumer::EngineSession& session, const QString& path,
                                            const std::optional<QSize>& maxSize, const bool withDetail)
{
    FilePreviews previews;
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = path}, std::stop_token{}, nullptr);
    if (!read.hasValue())
    {
        LOG_WARNING("PreviewThumbnailGenerator: '{}' could not be read: {}", path.toStdString(),
                    read.status().message());
        return previews;
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(std::stop_token{});
    if (!lease.hasValue())
    {
        return previews;
    }
    std::optional<PresentedPreview> whole =
        boundedPreview(session, lease->image(), maxSize.value_or(kDefaultMaximumReportPreviewSize), nullptr);
    if (!whole.has_value())
    {
        return previews;
    }
    if (withDetail)
    {
        previews.detail = detailPreview(session, *lease, whole->analysis);
    }
    previews.whole = std::move(whole->image);
    return previews;
}

[[nodiscard]] std::optional<QImage> previewFromPath(const an::consumer::EngineSession& session, const QString& path,
                                                    const std::optional<QSize>& maxSize)
{
    return previewsFromPath(session, path, maxSize, false).whole;
}

/// Writes a picture as a JPEG of the quality stated.
[[nodiscard]] bool writeJpeg(const QImage& image, const QString& path, const int quality)
{
    QImageWriter writer(path, "JPG");
    writer.setQuality(std::clamp(quality, 1, 100));
    writer.setOptimizedWrite(true);
    return writer.write(image);
}

[[nodiscard]] bool prepareOutputDirectory(const QString& outputPath, const QString& outputDir, const QString& fileName)
{
    if (outputPath.trimmed().isEmpty() || outputDir.trimmed().isEmpty() || fileName.trimmed().isEmpty() ||
        !QFileInfo::exists(outputPath))
    {
        return false;
    }
    QDir outDir(outputDir);
    return outDir.exists() || outDir.mkpath(QStringLiteral("."));
}
} // namespace

std::optional<QImage> generateBoundedPreview(const an::consumer::EngineSession& session,
                                             const an::consumer::AnyImage& image, const QSize& maxSize,
                                             PreviewGenerationDiagnostics* diagnostics)
{
    std::optional<PresentedPreview> preview = boundedPreview(session, image, maxSize, diagnostics);
    if (!preview.has_value())
    {
        return std::nullopt;
    }
    return std::move(preview->image);
}

QRect reportDetailRegion(const QSize& imageSize)
{
    if (imageSize.width() <= 0 || imageSize.height() <= 0)
    {
        return {};
    }
    const int width = (std::min)(imageSize.width(), kReportDetailRegionSize.width());
    const int height = (std::min)(imageSize.height(), kReportDetailRegionSize.height());
    return QRect((imageSize.width() - width) / 2, (imageSize.height() - height) / 2, width, height);
}

std::optional<QString> generatePreviewThumbnail(const an::consumer::EngineSession& session, const QString& outputPath,
                                                const QString& runDir, const QSize& maxSize)
{
    if (outputPath.trimmed().isEmpty() || runDir.trimmed().isEmpty() || !QFileInfo::exists(outputPath))
    {
        return std::nullopt;
    }
    const auto preview = previewFromPath(session, outputPath, maxSize);
    if (!preview.has_value())
    {
        return std::nullopt;
    }
    const QString previewName = QStringLiteral("preview.png");
    if (!preview->save(QDir(runDir).filePath(previewName), "PNG"))
    {
        return std::nullopt;
    }
    return previewName;
}

std::optional<QString> generateReportPreviewJpeg(const an::consumer::EngineSession& session, const QString& outputPath,
                                                 const QString& outputDir, const QString& fileName,
                                                 std::optional<QSize> maxSize, int quality)
{
    if (!prepareOutputDirectory(outputPath, outputDir, fileName))
    {
        return std::nullopt;
    }
    const auto preview = previewFromPath(session, outputPath, maxSize);
    if (!preview.has_value() || !writeJpeg(*preview, QDir(outputDir).filePath(fileName), quality))
    {
        return std::nullopt;
    }
    return fileName;
}

ReportMasterPreviews generateReportMasterPreviews(const an::consumer::EngineSession& session, const QString& outputPath,
                                                  const QString& outputDir, const QString& wholeName,
                                                  const QString& detailName, std::optional<QSize> maxSize, int quality)
{
    ReportMasterPreviews written;
    if (!prepareOutputDirectory(outputPath, outputDir, wholeName) || detailName.trimmed().isEmpty())
    {
        return written;
    }
    const FilePreviews previews = previewsFromPath(session, outputPath, maxSize, true);
    if (!previews.whole.has_value() || !writeJpeg(*previews.whole, QDir(outputDir).filePath(wholeName), quality))
    {
        return written;
    }
    written.whole = wholeName;
    if (previews.detail.has_value() && writeJpeg(*previews.detail, QDir(outputDir).filePath(detailName), quality))
    {
        written.detail = detailName;
    }
    return written;
}

std::optional<QString> generateReportPreviewPng(const an::consumer::EngineSession& session, const QString& outputPath,
                                                const QString& outputDir, const QString& fileName,
                                                std::optional<QSize> maxSize)
{
    if (!prepareOutputDirectory(outputPath, outputDir, fileName))
    {
        return std::nullopt;
    }
    const auto preview = previewFromPath(session, outputPath, maxSize);
    if (!preview.has_value() || !preview->save(QDir(outputDir).filePath(fileName), "PNG"))
    {
        return std::nullopt;
    }
    return fileName;
}
} // namespace ImageIntegration
