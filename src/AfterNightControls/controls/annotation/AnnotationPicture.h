// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The steps that make an annotated picture of a solved image, each on a worker: the bundled catalogues' entries in the
/// image's field, the image presented at the picture's size, the picture drawn - the presentation under the
/// annotations, or the annotations alone over transparent pixels - and the picture as an image of the session that a
/// view shows or a file holds.
///
/// **The picture is presented as the view presents.** The base is the view's presentation
/// (`widgets/ViewPresentation.h`) of the whole image, at the largest display-proxy reduction that leaves it at least
/// the picture's size and stretched through the whole image's analysis, so a picture smaller than its image is
/// stretched as the image is and every sample of the image reaches it.
///
/// **The picture is an image of its own.** It is eight-bit display RGB - with alpha over a transparent background -
/// recorded nonlinear, and it carries what still describes it: the frame's observing context and integration
/// (`derivedFrameCards`) and its solution at the picture's scale, so a picture can be solved, measured and annotated
/// again.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QImage>
#include <QSize>
#include <QSizeF>

#include <cstdint>
#include <memory>
#include <stop_token>
#include <vector>

namespace an::ui
{

/// The bundled catalogues' entries in and around a field.
struct AnnotationCatalogEntries
{
    /// Every deep-sky object, whatever its magnitude.
    std::vector<an::CatalogEntry> deepSky;

    /// Every bright star.
    std::vector<an::CatalogEntry> stars;
};

/// Reads both bundled catalogues and finds their entries within the field's radius and two degrees more, so an object
/// centred outside the frame that reaches into it is found. For a worker: each read and the query wait for admission.
///
/// @param session the process's session.
/// @param solution the image's solution.
/// @param source where the catalogues' text comes from; empty reads the product's resources.
/// @param stop the request's stop.
/// @return the entries; or what a read, an admission or a query refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<AnnotationCatalogEntries>
queryAnnotationCatalogs(const an::consumer::EngineSession& session, const an::WcsSolution& solution,
                        const BundledCatalogSource& source, std::stop_token stop);

/// The presentation of an image a picture is drawn over.
struct AnnotationBase
{
    /// The presented bytes, opaque, blue-first and premultiplied: what a painter draws.
    std::shared_ptr<const PresentedImage> presented;

    /// The part of the image, in its own pixels from its origin, the bytes cover: the whole image less the partial
    /// blocks a reduction leaves out at its right and bottom edges.
    QSizeF covers;

    /// The analysis the presentation was stretched through, which a later presentation of the image reuses.
    an::DisplayPresentationAnalysis analysis{};

    /// The bytes as an image a painter reads; it keeps them alive.
    [[nodiscard]] QImage image() const
    {
        return presentedQImage(presented);
    }
};

/// The presentation settings a picture's stretch takes: the view's own for `AnnotationStretch::AsShown` - the whole
/// image rather than a channel of it - or every channel at full scale, nothing stretched, for
/// `AnnotationStretch::AsStored`. Either is presented as blue-first premultiplied bytes.
///
/// @param shown what the view presents its document with, or nothing when it is not known.
/// @param stretch the picture's stretch.
/// @param linear whether the image is linear data, for a view whose settings are not known: such an image is stretched
///        as the view's automatic stretch shows it.
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API ViewPresentationSettings annotationPresentationSettings(
    const std::optional<ViewPresentationSettings>& shown, AnnotationStretch stretch, bool linear);

/// The display-proxy reduction an image is presented through for a picture of a size: the largest that leaves the
/// presentation at least the picture's size, so what is drawn of it is reduced by less than two more - or magnified -
/// on its way to the picture. A picture the image's size or larger takes the image's own pixels.
///
/// @param imageSize the image's size.
/// @param pictureSize the picture's size.
/// @return the reduction; 1 for an empty size.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::uint32_t annotationPresentationFactor(QSize imageSize,
                                                                                QSize pictureSize) noexcept;

/// Presents an image for a picture of a size: through the largest display-proxy reduction that leaves it at least that
/// size (`annotationPresentationFactor`), stretched through the handed analysis or, when none is handed or it is
/// refused, the whole image's measured here.
///
/// @param scope the presentation's admitted operation.
/// @param image the image, under a read lease held for the call.
/// @param settings what to present, from `annotationPresentationSettings`.
/// @param analysis the analysis to stretch through, or null.
/// @param memory the session's consumer memory the bytes are counted against, or null.
/// @param pictureSize the picture's size.
/// @return the presentation; or what the reduction or the presentation refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<AnnotationBase>
presentAnnotationBase(const an::consumer::OperationScope& scope, const an::consumer::AssetReadLease& image,
                      const ViewPresentationSettings& settings, const an::DisplayPresentationAnalysis* analysis,
                      an::consumer::ConsumerMemory* memory, QSize pictureSize);

/// A drawn picture and the memory it is counted against.
struct AnnotatedPicture
{
    /// The picture, premultiplied ARGB.
    QImage image;

    /// The reservation its pixels hold while it lives, when it was counted.
    an::consumer::MemoryReservation reservation;
};

/// Draws a picture of a size: the base scaled over it, or transparent pixels when there is no base, and the
/// annotations over that.
///
/// @param base the presentation drawn under the annotations, or null for a transparent picture.
/// @param pictureSize the picture's size.
/// @param scene the scene.
/// @param layout the scene's layout.
/// @param style the style.
/// @param memory the session's consumer memory the picture is counted against, as a display buffer, or null.
/// @return the picture; `ErrorCode::InvalidArgument` for an empty size; `ErrorCode::AllocationFailed` when its pixels
///         cannot be had; or what the memory's allowance refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<AnnotatedPicture>
composeAnnotatedPicture(const AnnotationBase* base, QSize pictureSize, const AnnotationScene& scene,
                        const AnnotationLayout& layout, const AnnotationStyle& style,
                        an::consumer::ConsumerMemory* memory);

/// How a picture is described as an image: eight-bit display RGB, with alpha when it keeps its transparency, its
/// samples a nonlinear encoding of full scale. What an export decides its output intent from before the picture is
/// drawn.
///
/// @param limits the engine's limits the extent is held to.
/// @param size the picture's size.
/// @param alpha whether the picture keeps its alpha.
/// @return the description, or nothing for a size the limits refuse.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::ImageDescriptor>
annotatedPictureDescriptor(const an::EngineLimits& limits, QSize size, bool alpha);

/// A picture as an image of the session: eight-bit RGB, or RGBA when it is to keep its transparency, recorded
/// nonlinear, with the frame's observing context and integration and its solution at the picture's scale.
///
/// @param session the process's session.
/// @param picture the picture.
/// @param frame the image's cards the picture was made of.
/// @param solution the image's solution.
/// @param alpha whether the picture keeps its alpha.
/// @param stop the request's stop.
/// @return the image; or what the admission, the image's creation or its cards refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
annotatedPictureAsset(const an::consumer::EngineSession& session, const QImage& picture, const an::CodecMetadata& frame,
                      const an::WcsSolution& solution, bool alpha, std::stop_token stop);

} // namespace an::ui
