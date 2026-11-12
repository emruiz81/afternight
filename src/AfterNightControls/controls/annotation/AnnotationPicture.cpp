// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotationPicture.h"

#include "controls/AutostretchApply.h"
#include "controls/ImageViewFiles.h"
#include "controls/annotation/AnnotationRenderer.h"
#include "session/DisplayProxy.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace an::ui
{
namespace
{

/// How far past the field's radius the catalogues are asked, so an object centred outside the frame that reaches into
/// it is found.
constexpr double kCatalogMarginDegrees = 2.0;

[[nodiscard]] an::Status refused(const an::ErrorCode code, const char* message)
{
    return an::Status::failure(code, message);
}

/// The width and height of the image a lease reads.
[[nodiscard]] QSize leasedSize(const an::consumer::AssetReadLease& image)
{
    QSize size;
    image.visit([&size](const auto& typed) {
        if (const std::optional<an::ImageDescriptor> described = typed.descriptor(); described.has_value())
        {
            const an::ImageExtent& extent = described->extent();
            size = QSize(static_cast<int>(extent.width()), static_cast<int>(extent.height()));
        }
    });
    return size;
}

/// The picture's solution: the image's, resampled to the picture's size. A polynomial that the two axes' scales no
/// longer describe alike is dropped for the linear solution, which still places every object within a pixel or so of
/// the picture's smaller grid.
[[nodiscard]] an::Result<an::WcsSolution> pictureSolution(const an::WcsSolution& solution, const QSize picture)
{
    const auto width = static_cast<std::uint32_t>(picture.width());
    const auto height = static_cast<std::uint32_t>(picture.height());
    if (solution.imageWidth() == width && solution.imageHeight() == height)
    {
        return solution;
    }
    const double scaleX = static_cast<double>(width) / static_cast<double>(solution.imageWidth());
    const double scaleY = static_cast<double>(height) / static_cast<double>(solution.imageHeight());
    an::Result<an::WcsSolution> resized = an::resizeWcsSolution(solution, scaleX, scaleY, width, height);
    if (!resized.hasValue() && resized.status().code() == an::ErrorCode::Unsupported)
    {
        resized = an::resizeWcsSolution(solution.withoutDistortion(), scaleX, scaleY, width, height);
    }
    return resized;
}

} // namespace

an::Result<AnnotationCatalogEntries> queryAnnotationCatalogs(const an::consumer::EngineSession& session,
                                                             const an::WcsSolution& solution,
                                                             const BundledCatalogSource& source, std::stop_token stop)
{
    const an::Result<an::SkyCoordinate> centre = solution.fieldCenter();
    const an::Result<double> radius = solution.queryRadiusDegrees(0.0);
    if (!centre.hasValue() || !radius.hasValue())
    {
        return refused(an::ErrorCode::InvalidArgument, "the solution places no field on the sky");
    }
    AnnotationCatalogEntries entries;
    for (const BundledCatalog catalog : {BundledCatalog::DeepSky, BundledCatalog::BrightStars})
    {
        // A picture asks only what lies in its field, so the catalog has no name index: the product's own is the one
        // every reader of fields shares, and a source the picture was given is read for it alone.
        std::shared_ptr<const an::SkyCatalog> loaded;
        if (source)
        {
            an::Result<an::SkyCatalog> read = loadBundledCatalog(session, catalog, stop, source, false);
            if (!read.hasValue())
            {
                return read.status();
            }
            loaded = std::make_shared<const an::SkyCatalog>(std::move(*read));
        }
        else
        {
            an::Result<std::shared_ptr<const an::SkyCatalog>> shared = sharedBundledCatalog(session, catalog, stop);
            if (!shared.hasValue())
            {
                return shared.status();
            }
            loaded = std::move(*shared);
        }
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::CatalogRegionQuery query;
        query.center = *centre;
        query.radiusDegrees = (std::min)(180.0, *radius + kCatalogMarginDegrees);
        const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(scope->execution(), *loaded, query);
        if (!found.hasValue())
        {
            return found.status();
        }
        std::vector<an::CatalogEntry>& kept = catalog == BundledCatalog::DeepSky ? entries.deepSky : entries.stars;
        kept.reserve(found->entryIndices.size());
        for (const std::uint32_t index : found->entryIndices)
        {
            if (const an::CatalogEntry* entry = loaded->entry(index); entry != nullptr)
            {
                kept.push_back(*entry);
            }
        }
        scope->end();
    }
    return entries;
}

ViewPresentationSettings annotationPresentationSettings(const std::optional<ViewPresentationSettings>& shown,
                                                        const AnnotationStretch stretch, const bool linear)
{
    ViewPresentationSettings settings;
    if (stretch == AnnotationStretch::AsShown && shown.has_value())
    {
        settings = *shown;
    }
    else if (stretch == AnnotationStretch::AsShown && linear)
    {
        // Each colour channel takes its own stretch, as the view's automatic stretch shows an image unless asked to
        // link them.
        AutostretchApplySettings automatic;
        automatic.linkedChannels = false;
        settings.request = autostretchApplyRequest(automatic);
    }
    else
    {
        settings.request.stretch = an::PresentationStretch::Linear;
    }
    settings.channel = ViewChannel{};
    settings.subsampleFactor = 1u;
    settings.proxyFactor = 1u;
    settings.region.reset();
    settings.request.output = an::PresentationOutput::Bgra8;
    return settings;
}

std::uint32_t annotationPresentationFactor(const QSize imageSize, const QSize pictureSize) noexcept
{
    if (imageSize.isEmpty() || pictureSize.isEmpty())
    {
        return 1u;
    }
    const auto width = static_cast<std::uint32_t>(imageSize.width());
    const auto height = static_cast<std::uint32_t>(imageSize.height());
    const double across = static_cast<double>(width) / static_cast<double>(pictureSize.width());
    const double down = static_cast<double>(height) / static_cast<double>(pictureSize.height());
    const auto wanted = static_cast<std::uint32_t>((std::max)(1.0, std::floor((std::min)(across, down))));
    return (std::max)(1u, (std::min)(wanted, largestDisplayProxyFactor(width, height)));
}

an::Result<AnnotationBase> presentAnnotationBase(const an::consumer::OperationScope& scope,
                                                 const an::consumer::AssetReadLease& image,
                                                 const ViewPresentationSettings& settings,
                                                 const an::DisplayPresentationAnalysis* analysis,
                                                 an::consumer::ConsumerMemory* memory, const QSize pictureSize)
{
    const QSize imageSize = leasedSize(image);
    if (imageSize.isEmpty() || pictureSize.isEmpty())
    {
        return refused(an::ErrorCode::InvalidArgument, "the image or the picture has no size");
    }
    const std::uint32_t factor = annotationPresentationFactor(imageSize, pictureSize);

    ViewPresentationSettings presentation = settings;
    presentation.proxyFactor = factor;
    presentation.region.reset();
    std::shared_ptr<const DisplayProxy> proxy;
    if (factor >= 2u)
    {
        an::Result<std::shared_ptr<const DisplayProxy>> built =
            DisplayProxy::build(scope.engine(), scope.execution(), image, factor);
        if (!built.hasValue())
        {
            return built.status();
        }
        proxy = std::move(*built);
    }
    an::Result<ViewPresentationOutcome> presented =
        presentForView(scope.engine(), scope.execution(), image, presentation, analysis, memory, proxy.get(),
                       WholeMeasurement::Measured);
    if (!presented.hasValue())
    {
        return presented.status();
    }
    AnnotationBase base;
    base.presented = std::move(presented->image);
    base.covers = QSizeF(presented->covered.width, presented->covered.height);
    base.analysis = presented->analysis;
    return base;
}

an::Result<AnnotatedPicture> composeAnnotatedPicture(const AnnotationBase* base, const QSize pictureSize,
                                                     const AnnotationScene& scene, const AnnotationLayout& layout,
                                                     const AnnotationStyle& style, an::consumer::ConsumerMemory* memory)
{
    if (pictureSize.isEmpty() || scene.imageSize.isEmpty())
    {
        return refused(an::ErrorCode::InvalidArgument, "the picture has no size");
    }
    AnnotatedPicture picture;
    if (memory != nullptr)
    {
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(pictureSize.width()) * static_cast<std::uint64_t>(pictureSize.height()) * 4u;
        an::Result<an::consumer::MemoryReservation> reserved =
            memory->reserve(an::consumer::MemoryCategory::DisplayBuffers, bytes);
        if (!reserved.hasValue())
        {
            return reserved.status();
        }
        picture.reservation = std::move(*reserved);
    }
    picture.image = QImage(pictureSize, QImage::Format_ARGB32_Premultiplied);
    if (picture.image.isNull())
    {
        return refused(an::ErrorCode::AllocationFailed, "the picture's pixels could not be had");
    }
    const bool opaque = base != nullptr && base->presented != nullptr;
    picture.image.fill(opaque ? Qt::black : Qt::transparent);

    const double scaleX = static_cast<double>(pictureSize.width()) / scene.imageSize.width();
    const double scaleY = static_cast<double>(pictureSize.height()) / scene.imageSize.height();
    QPainter painter(&picture.image);
    if (opaque)
    {
        const QImage source = base->image();
        const QSize target(
            (std::min)(pictureSize.width(), static_cast<int>(std::lround(base->covers.width() * scaleX))),
            (std::min)(pictureSize.height(), static_cast<int>(std::lround(base->covers.height() * scaleY))));
        if (!target.isEmpty())
        {
            // The smooth scaling averages the pixels a reduction folds together, which a painter's own filter does not.
            const QImage scaled = source.size() == target
                                      ? source
                                      : source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(QPointF(0.0, 0.0), scaled);
            // A reduction leaves out the partial blocks at the image's right and bottom edges, so what it presents
            // may end a column or a row short of the picture - under a pixel of it, since the reduction is never
            // larger than the picture's own. That last column and row are the image's too: they take the pixels
            // beside them rather than stay the colour the picture was cleared to.
            const int missingColumns = pictureSize.width() - target.width();
            const int missingRows = pictureSize.height() - target.height();
            if (missingColumns > 0)
            {
                painter.drawImage(QRect(target.width(), 0, missingColumns, target.height()), scaled,
                                  QRect(target.width() - 1, 0, 1, target.height()));
            }
            if (missingRows > 0)
            {
                painter.drawImage(QRect(0, target.height(), target.width(), missingRows), scaled,
                                  QRect(0, target.height() - 1, target.width(), 1));
                if (missingColumns > 0)
                {
                    painter.fillRect(QRect(target.width(), target.height(), missingColumns, missingRows),
                                     scaled.pixelColor(target.width() - 1, target.height() - 1));
                }
            }
        }
    }
    painter.scale(scaleX, scaleY);
    paintAnnotations(painter, scene, layout, style);
    painter.end();
    return picture;
}

std::optional<an::ImageDescriptor> annotatedPictureDescriptor(const an::EngineLimits& limits, const QSize size,
                                                              const bool alpha)
{
    if (size.width() <= 0 || size.height() <= 0)
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
        static_cast<std::uint32_t>(size.width()), static_cast<std::uint32_t>(size.height()), alpha ? 4u : 3u, limits);
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(
        an::ImageDescriptorRequest{.extent = *extent,
                                   .sampleType = an::SampleType::UInt8,
                                   .channelSchema = alpha ? an::ChannelSchema::Rgba : an::ChannelSchema::Rgb,
                                   .colorSpace = an::ColorSpace::DisplayRgb,
                                   .numericDomain = an::NumericDomain::EncodedNonlinear,
                                   .cfa = an::CfaDescriptor{},
                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                   .pixelAspectRatio = 1.0});
}

namespace
{
/// The rows of a picture laid out into its image at a time.
constexpr std::uint32_t kPictureBandRows = 256u;
} // namespace

an::Result<an::consumer::ImageAsset> annotatedPictureAsset(const an::consumer::EngineSession& session,
                                                           const QImage& picture, const an::CodecMetadata& frame,
                                                           const an::WcsSolution& solution, const bool alpha,
                                                           std::stop_token stop)
{
    if (picture.isNull() || picture.width() <= 0 || picture.height() <= 0)
    {
        return refused(an::ErrorCode::InvalidArgument, "the picture has no pixels");
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const auto width = static_cast<std::uint32_t>(picture.width());
    const auto height = static_cast<std::uint32_t>(picture.height());
    const std::uint32_t channels = alpha ? 4u : 3u;
    const std::optional<an::ImageDescriptor> descriptor =
        annotatedPictureDescriptor(scope->engine().limits(), picture.size(), alpha);
    if (!descriptor.has_value())
    {
        return refused(an::ErrorCode::InvalidArgument, "the picture is larger than an image may be");
    }
    const an::ImageExtent& extent = descriptor->extent();

    // What still describes the picture: the frame's observing context and integration, its solution at the picture's
    // scale, and that its samples are no longer linear.
    an::Result<an::CodecMetadata> cards = derivedFrameCards(frame);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    const an::Result<an::WcsSolution> placed = pictureSolution(solution, picture.size());
    if (placed.hasValue())
    {
        cards = an::withWcsCards(*cards, *placed);
        if (!cards.hasValue())
        {
            return cards.status();
        }
    }
    cards = an::withProcessingStateCards(
        *cards, an::nonlinearState(an::readProcessingStateCards(*cards), *descriptor, "Annotate Image"));
    if (!cards.hasValue())
    {
        return cards.status();
    }
    an::ImageSemanticsRequest meaning;
    meaning.transferFunction = an::TransferFunction::Srgb;
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  std::move(*cards), std::nullopt);

    an::Result<an::Image<std::uint8_t>> image =
        an::Image<std::uint8_t>::create(scope->engine(), *descriptor, an::StorageArrangement::Planar,
                                        an::StorageInitialization::Uninitialized, std::move(metadata));
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        std::optional<an::ImageWriteAccess<std::uint8_t>> access = image->write();
        if (!access.has_value())
        {
            return refused(an::ErrorCode::NotInitialized, "the picture's image holds nothing");
        }
        // The painter's premultiplied pixels as straight bytes, laid out a plane per channel. They are taken a band
        // of rows at a time, so the picture is never held a second and a third time whole beside the image it
        // becomes: a band's interleaved bytes are all that is held besides.
        const an::ImageLayout& layout = access->view().layout();
        std::uint8_t* const samples = access->view().data();
        if (samples == nullptr || layout.extent().width() != width || layout.extent().height() != height ||
            layout.planeCount() != channels)
        {
            return refused(an::ErrorCode::Internal, "the picture's image is not laid out a plane per channel");
        }
        const auto rowStride = static_cast<std::size_t>(layout.rowStrideBytes());
        const auto planeStride = static_cast<std::size_t>(layout.planeStrideBytes());
        for (std::uint32_t first = 0u; first < height; first += kPictureBandRows)
        {
            if (an::Status stopped = scope->execution().checkCancellation(); stopped.failed())
            {
                return stopped;
            }
            const std::uint32_t rows = (std::min)(kPictureBandRows, height - first);
            const QImage band =
                picture.copy(0, static_cast<int>(first), static_cast<int>(width), static_cast<int>(rows))
                    .convertToFormat(alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
            if (band.isNull())
            {
                return refused(an::ErrorCode::AllocationFailed, "the picture's pixels could not be laid out");
            }
            for (std::uint32_t row = 0u; row < rows; ++row)
            {
                const uchar* const interleaved = band.constScanLine(static_cast<int>(row));
                for (std::uint32_t channel = 0u; channel < channels; ++channel)
                {
                    std::uint8_t* const plane = samples + planeStride * channel + rowStride * (first + row);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        plane[x] = interleaved[static_cast<std::size_t>(x) * channels + channel];
                    }
                }
            }
        }
    }
    scope->end();
    return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
}

} // namespace an::ui
