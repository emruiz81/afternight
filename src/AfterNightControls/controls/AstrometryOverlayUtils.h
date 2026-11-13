// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the astrometry overlays share: the part of the image a view shows, the sky that part covers, where a position
/// in the image is drawn, and the bundled-catalog query an annotation overlay runs on a worker.
///
/// **Engine pixel coordinates are the widget's.** The view draws pixel column `i` over `[i, i + 1)` times its scale,
/// and an engine pixel coordinate puts the centre of column `i` at `i + 0.5`, so a position the solution answers is
/// drawn at that position times the scale.
///
/// **A catalog query is a worker's.** A query needs an admitted operation, and the GUI thread never waits for one, so
/// an annotation overlay asks a worker for the catalog's entries inside the whole image's field and paints what the
/// last answer held; scrolling and zooming project those entries again without asking.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
#include "controls/annotation/AnnotationSettings.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QWidget>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class ImageViewControl;

namespace an::ui
{
struct AnnotationLayout;
struct AnnotationScene;
} // namespace an::ui

namespace an::ui::detail
{

/// Where an engine pixel coordinate is drawn in a view's widget.
///
/// @param pixel the position.
/// @param scale the view's scale.
/// @return the widget position.
[[nodiscard]] inline QPointF widgetPoint(const an::PixelCoordinate& pixel, const double scale)
{
    return QPointF(pixel.x * scale, pixel.y * scale);
}

/// What a projection cached for one view state depends on beside the solution: the scale, the scroll offsets and the
/// sizes of the viewport and the widget.
struct OverlayViewState
{
    double scale = 0.0;
    int horizontalOffset = 0;
    int verticalOffset = 0;
    QSize viewportSize;
    QSize widgetSize;

    [[nodiscard]] bool matches(const OverlayViewState& other) const
    {
        return qFuzzyCompare(scale + 1.0, other.scale + 1.0) && horizontalOffset == other.horizontalOffset &&
               verticalOffset == other.verticalOffset && viewportSize == other.viewportSize &&
               widgetSize == other.widgetSize;
    }
};

/// The view state an overlay of this view projects for now.
[[nodiscard]] OverlayViewState overlayViewState(ImageViewControl* control);

/// The part of a view's widget its viewport shows at the scroll offsets: the whole widget when it fits.
///
/// @param control the view.
/// @return the part, in the widget's pixels; empty without a widget or a scroll area.
[[nodiscard]] AFTERNIGHTCONTROLS_API QRectF visibleOverlayWidgetRect(ImageViewControl* control);

/// The sky the visible part of an image covers: the direction at its centre, and the radius of the cone that holds the
/// directions sampled along its edges.
struct VisibleSky
{
    an::SkyCoordinate center{};
    double radiusDegrees = 0.0;
};

/// The sky a rectangle of the image covers, sampled along its edges.
///
/// @param solution the image's solution.
/// @param visibleImageRect the rectangle, in image coordinates.
/// @return the sky, or nothing when the solution cannot place the rectangle's centre or the radius is not positive.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<VisibleSky> visibleSky(const an::WcsSolution& solution,
                                                                          const QRectF& visibleImageRect);

/// The choices a live overlay's scene is built with at a level of detail, so the view keeps, merges and names the
/// objects as an annotated picture does: the overlay's catalog's layer alone and the level's catalogues, kinds,
/// magnitudes and stars - but every size, since the view holds an object to the level's smallest size for the part of
/// the image it shows when it lays the labels out (`overlaySmallestObject`).
///
/// @param catalog the overlay's catalog.
/// @param detail the level; `AnnotationDetail::Custom` is taken as the standard one.
/// @return the choices.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationContent overlayAnnotationContent(BundledCatalog catalog,
                                                                                AnnotationDetail detail);

/// The smallest deep-sky object a level of detail names that is not notable: its major axis as a fraction of the
/// reference length of what is shown - a picture's frame, or the part of the image a view shows
/// (`overlayShownReferenceLength`).
///
/// @param detail the level.
/// @return the fraction; zero names every size.
[[nodiscard]] AFTERNIGHTCONTROLS_API double overlaySmallestObject(AnnotationDetail detail);

/// The reference length of the part of an image a view shows at a scale: the geometric mean of the part's sides in the
/// image's pixels - each the image's own side where it fits the viewport - rounded to a quarter of a doubling, so a
/// change of a few pixels, as a scroll bar's appearing makes, lays nothing out again while a zoom step does.
///
/// @param imageSize the image's size, in its own pixels.
/// @param viewportSize the size of the viewport the view shows the image in, in the widget's pixels.
/// @param scale the view's scale: its widget's pixels to the image's.
/// @return the length in the image's pixels; zero for an empty size or a scale that is not positive.
[[nodiscard]] AFTERNIGHTCONTROLS_API double overlayShownReferenceLength(QSize imageSize, QSizeF viewportSize,
                                                                        double scale) noexcept;

/// The magnitude limit a catalogue's query of a field states for a scene's choices: the stars' limit, and the deep-sky
/// objects' unless the choices name objects of no measured magnitude, or every magnitude - a limit would leave those
/// out.
///
/// @param catalog the catalogue queried.
/// @param content the choices.
/// @return the limit, or nothing for none.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<double> overlayQueryMagnitudeLimit(BundledCatalog catalog,
                                                                                      const AnnotationContent& content);

/// What a view's catalog overlay draws at a zoom: its layers' marks in one scene and where their labels go.
struct OverlayAnnotationLayout
{
    /// The marks drawn, most prominent first, each labelled on one line, with every length set for the zoom.
    std::shared_ptr<const AnnotationScene> scene;

    /// Where their labels go.
    std::shared_ptr<const AnnotationLayout> layout;
};

/// Lays out what a view's catalog overlay draws at a zoom, as an annotated picture lays out its labels: the deep-sky
/// objects' and the stars' marks in one scene, ordered as a scene of both is, each labelled as the choice of label
/// says, and every length set so a label is `fontPixels` tall on the screen at `scale` - at every zoom alike, the name
/// under a designation as tall as the designation, where a picture sets it a step smaller. A deep-sky object smaller
/// than `smallestMajorPixels` that is not notable is left out, as a picture leaves it out. No label covers another;
/// every deep-sky object kept keeps its outline, a star the level marks without naming it is drawn without a label, and
/// a star whose label found no room is left out, as the view's star labels leave it out. Lays out on any thread a
/// `QGuiApplication` lets read fonts on.
///
/// @param deepSky the deep-sky layer's scene, or null when it is not drawn.
/// @param stars the star layer's scene, or null when it is not drawn.
/// @param scale the view's scale: its widget's pixels to the image's.
/// @param fontPixels a label's size on the screen, in the widget's pixels.
/// @param style the style the marks and labels are drawn in.
/// @param labelText what a label says.
/// @param smallestMajorPixels the smallest major axis of an object that is not notable, in the image's pixels.
/// @return the layout; an empty one for no layer, no frame or a scale or size that is not positive.
[[nodiscard]] AFTERNIGHTCONTROLS_API OverlayAnnotationLayout
layoutOverlayAnnotations(const AnnotationScene* deepSky, const AnnotationScene* stars, double scale, double fontPixels,
                         const AnnotationStyle& style, AnnotationLabelText labelText, double smallestMajorPixels);

/// What a view's grid draws: an annotated picture's grid of the part of the image the view shows, as a scene whose
/// frame is that part, and where the grid's labels go at its edges.
struct OverlayGridLayout
{
    /// The grid, its frame the part shown with its origin at the part's top-left corner.
    std::shared_ptr<const AnnotationScene> scene;

    /// Where the grid's labels go.
    std::shared_ptr<const AnnotationLayout> layout;

    /// Where the scene's origin lies in the image, in its own pixels.
    QPointF origin;

    /// How many of the scene's units make one image pixel. A scene's frame is a whole number of its units, and the
    /// part a view shows is not a whole number of image pixels: zoomed in, the frame rounded up to whole pixels ended
    /// most of a pixel - tens of screen pixels - past what the view shows, and the labels set against its right and
    /// bottom edges were cut off or hidden. The scene is laid out in units no larger than a screen pixel instead, so
    /// its frame ends within one of the part shown.
    double unitsPerPixel = 1.0;
};

/// Lays out the equatorial grid of the part of an image a view shows, as an annotated picture lays out its grid: the
/// picture's lines of that part (`buildAnnotationGrid`), spaced for the part's own sky, labelled where they meet its
/// edges, every length set so a label is `fontPixels` tall on the screen at `scale`.
///
/// @param solution the image's solution.
/// @param part the part shown, in the image's own pixels.
/// @param scale the view's scale: its widget's pixels to the image's.
/// @param fontPixels a label's size on the screen, in the widget's pixels.
/// @param style the style the grid is drawn in.
/// @return the layout; an empty one for an empty part or a scale or size that is not positive.
[[nodiscard]] AFTERNIGHTCONTROLS_API OverlayGridLayout layoutOverlayGrid(const an::WcsSolution& solution,
                                                                         const QRectF& part, double scale,
                                                                         double fontPixels,
                                                                         const AnnotationStyle& style);

/// The size an overlay's label is set at on the screen: the theme's caption at the view's text size, in a widget's
/// pixels. A caption stated in points is set at the widget's logical resolution, which needs no font of the platform's:
/// a platform with none resolves no pixel size.
///
/// @param widget the widget the label is drawn on.
/// @param size the view's text size.
/// @return the size, in the widget's pixels.
[[nodiscard]] AFTERNIGHTCONTROLS_API double overlayLabelPixels(const QWidget& widget, AnnotationViewTextSize size);

/// The entries a bundled catalog holds inside an image's field, for a scene's choices, as a worker found them.
struct FieldCatalogEntries
{
    /// The solution the field was the field of.
    an::WcsSolution solution;

    /// The choices the scene was built with (`overlayAnnotationContent`).
    AnnotationContent content;

    /// The entries, in the order the catalog's region query answers - brightest first - at the limit the choices let
    /// the query state (`overlayQueryMagnitudeLimit`).
    std::vector<an::CatalogEntry> entries;

    /// The overlay's scene of the entries: what an annotated picture keeps of them under the choices, entries of one
    /// object merged under one designation, each object projected through the solution, most prominent first.
    std::shared_ptr<const AnnotationScene> scene;
};

/// One overlay's queries of a bundled catalog: the catalog, read by the first query on a worker and kept for the
/// overlay's life, and the answer of the query asked for last.
///
/// Shared between the overlay and the workers it asks; the overlay drops its share when it goes, and an answer that
/// lands after that is never drawn.
class FieldCatalogQueries
{
public:
    /// Queries of one bundled catalog, whose answers repaint a widget.
    FieldCatalogQueries(BundledCatalog catalog, double marginDegrees);

    /// The answer for a solution and a scene's choices, when the last query that landed was for them; otherwise asks a
    /// worker for one - unless it is already asking - and answers nothing.
    ///
    /// @param solution the image's solution.
    /// @param content the choices the scene is built with (`overlayAnnotationContent`).
    /// @param repaint the widget the answer repaints when it lands.
    /// @return the entries, or null while the query runs or when it failed.
    [[nodiscard]] std::shared_ptr<const FieldCatalogEntries> answer(const an::WcsSolution& solution,
                                                                    const AnnotationContent& content, QWidget* repaint);

    /// How many queries were asked of workers.
    [[nodiscard]] std::uint64_t queriesAsked() const noexcept;

    /// Whether a query is on its way: an answer of null then means one is coming, not that the catalog failed.
    [[nodiscard]] bool isAsking() const noexcept;

    /// Reads the catalog's text from another source; a catalog already read is read again by the next query.
    ///
    /// @param source the source; empty reads the product's resources.
    void setSource(BundledCatalogSource source);

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace an::ui::detail
