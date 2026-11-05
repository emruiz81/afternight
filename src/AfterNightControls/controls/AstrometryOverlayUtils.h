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

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QWidget>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class ImageViewControl;

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

// Returns the visible widget rectangle, accounting for scroll position,
// or the full widget rect if scrollbars are not visible.
[[nodiscard]] QRectF visibleOverlayWidgetRect(ImageViewControl* control);

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

/// The entries a bundled catalog holds inside an image's field, at a magnitude limit, as a worker found them.
struct FieldCatalogEntries
{
    /// The solution the field was the field of.
    an::WcsSolution solution;

    /// The magnitude limit the entries were held to.
    double magnitudeLimit = 0.0;

    /// The entries, in the order the catalog's region query answers - brightest first.
    std::vector<an::CatalogEntry> entries;
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

    /// The answer for a solution and a limit, when the last query that landed was for them; otherwise asks a worker
    /// for one - unless it is already asking - and answers nothing.
    ///
    /// @param solution the image's solution.
    /// @param magnitudeLimit the faintest magnitude labelled.
    /// @param repaint the widget the answer repaints when it lands.
    /// @return the entries, or null while the query runs or when it failed.
    [[nodiscard]] std::shared_ptr<const FieldCatalogEntries> answer(const an::WcsSolution& solution,
                                                                    double magnitudeLimit, QWidget* repaint);

    /// How many queries were asked of workers.
    [[nodiscard]] std::uint64_t queriesAsked() const noexcept;

    /// Reads the catalog's text from another source; a catalog already read is read again by the next query.
    ///
    /// @param source the source; empty reads the product's resources.
    void setSource(BundledCatalogSource source);

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace an::ui::detail
