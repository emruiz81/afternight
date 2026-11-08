// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an annotated picture of a solved image draws, in the image's own pixels: the equatorial grid's lines clipped to
/// the frame and where they meet its edges, the deep-sky objects and bright stars a person's choices keep - each with
/// its outline or marker, its designation and name and how prominent it is - the directions of north and east and the
/// pixel scale at each corner, and the title and facts the panel states.
///
/// A scene is built once for a solution, the catalogues' entries in its field and the choices of what is shown, and
/// knows nothing of how it looks: the label layout (`controls/annotation/AnnotationLayout.h`) and the renderer
/// (`controls/annotation/AnnotationRenderer.h`) read it at any size. Building one projects every kept object through
/// the solution and reads no image, so it runs on any thread.
///
/// **Positions are engine pixel coordinates.** The centre of the first pixel is at one half, a pixel covers `[i, i +
/// 1)`, and the frame is the rectangle from the origin to the image's width and height, as the view draws it.
///
/// **One object, one mark.** The catalogues list some objects twice - `M 42` and `NGC 1976`, `NGC 3372` and `C 92` - so
/// entries of one common name close together, or at one position, are one mark under the designation people use
/// most: Messier, then NGC, then IC, then Caldwell.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/annotation/AnnotationSettings.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Image/CodecMetadata.h>

#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace an::ui
{

/// What kind of object a mark is.
enum class AnnotationMarkKind : std::uint8_t
{
    Galaxy,

    /// A bright or dark nebula, or a cluster with nebulosity.
    Nebula,

    PlanetaryNebula,

    OpenCluster,

    GlobularCluster,

    /// A catalogue entry of another kind: a star, an asterism, a knot, an entry of no known kind.
    OtherObject,

    /// A bright star.
    Star
};

/// The kind of object a deep-sky catalogue's type names.
///
/// @param objectType the catalogue's type, such as `Galaxy` or `Cluster with Nebulosity`.
/// @return the kind; `AnnotationMarkKind::OtherObject` for a type of no other kind.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationMarkKind annotationKindOfType(std::string_view objectType) noexcept;

/// One annotated object.
struct AnnotationMark
{
    /// What it is.
    AnnotationMarkKind kind = AnnotationMarkKind::OtherObject;

    /// Where it lies.
    QPointF center;

    /// Its outline - its size at its position angle - for an object of stated size; empty for a point.
    QPolygonF outline;

    /// A point's marker radius, in pixels; zero for an object with an outline.
    double radius = 0.0;

    /// Whether its outline covers the frame or reaches past it on both sides, so the frame is inside the object.
    bool coversFrame = false;

    /// Its designation: `M 42`, `NGC 3372`, `α Ori`, or a star's catalogue number, `HIP 27989`, when the choices name
    /// every star; empty for a star named by its proper name alone.
    QString designation;

    /// Its common name, or a star's proper name; empty when it has none.
    QString name;

    /// Its magnitude, when a catalogue states one.
    std::optional<double> magnitude;

    /// How prominent it is: a label of a higher priority is placed first.
    double priority = 0.0;

    /// Whether it is marked without a label: a star the choices mark but do not name.
    bool markOnly = false;
};

/// An edge of the frame.
enum class AnnotationEdge : std::uint8_t
{
    Top,
    Bottom,
    Left,
    Right
};

/// One line of the equatorial grid.
struct AnnotationGridLine
{
    /// Whether the line is one of constant right ascension.
    bool rightAscension = true;

    /// The constant coordinate, in degrees.
    double coordinateDegrees = 0.0;

    /// The parts of the line inside the frame.
    std::vector<QPolygonF> segments;

    /// Where the line meets the frame's edges.
    struct Crossing
    {
        QPointF point;
        AnnotationEdge edge = AnnotationEdge::Top;
    };

    /// The crossings, in the order the line is drawn.
    std::vector<Crossing> crossings;

    /// The coordinate's label: `10h 44m`, `-59° 40'`.
    QString label;
};

/// Which way north and east point, and how much sky a pixel spans, at one place in the frame.
struct AnnotationSkyAxes
{
    /// Where the directions were taken.
    QPointF origin;

    /// A unit vector toward celestial north, in pixels.
    QPointF north;

    /// A unit vector toward celestial east.
    QPointF east;

    /// The arcseconds one pixel spans along the frame's rows there.
    double arcsecondsPerPixel = 0.0;
};

/// One fact the panel states: what it is and its value, `Field` and `2.21° × 1.65°  ·  1.10″/px`.
struct AnnotationFact
{
    QString label;
    QString value;
};

/// What the panel states.
struct AnnotationPanelText
{
    /// The title: the person's, else the target the cards name, else the most prominent named object.
    QString title;

    /// The line under it.
    QString credit;

    /// The facts, one a line, in the order the choices list them.
    std::vector<AnnotationFact> facts;
};

/// What a picture of a solved image draws.
struct AnnotationScene
{
    /// The frame.
    QSize imageSize;

    /// The length every size is a fraction of (`annotationReferenceLength`).
    double referenceLength = 0.0;

    /// The grid's lines, of declination first, each set in increasing coordinate.
    std::vector<AnnotationGridLine> grid;

    /// The deep-sky objects and the stars, in decreasing priority.
    std::vector<AnnotationMark> marks;

    /// The directions and the scale at each corner, inset where a compass or a scale bar in that corner sits, indexed
    /// by `AnnotationCorner`; nothing where the solution places no sky.
    std::array<std::optional<AnnotationSkyAxes>, 4u> cornerAxes{};

    /// What the panel states.
    AnnotationPanelText panel;
};

/// What a scene is built from.
struct AnnotationSceneSources
{
    /// The image's solution; its frame is the image's.
    const an::WcsSolution* solution = nullptr;

    /// The deep-sky catalogue's entries in and around the field.
    std::span<const an::CatalogEntry> deepSky;

    /// The bright-star catalogue's entries in and around the field.
    std::span<const an::CatalogEntry> stars;

    /// The image's header cards, for the panel.
    std::span<const an::CodecCard> cards;
};

/// Builds the scene a picture draws: the grid, the marks the choices keep and the panel's text, as the choices say.
///
/// A layer the choices leave out builds nothing. A deep-sky entry is kept when its catalogue and kind are chosen, it is
/// bright and large enough or a Messier, Caldwell or named object, and some of it lies inside the frame; a star when it
/// is bright enough, lies inside the frame and the choices name it - by its proper name, its Bayer or Flamsteed
/// designation or its catalogue number, as they say - or mark the stars they do not name.
///
/// @param sources the solution, the catalogues' entries and the cards.
/// @param content the choices of what is shown.
/// @return the scene; an empty one without a solution.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationScene buildAnnotationScene(const AnnotationSceneSources& sources,
                                                                          const AnnotationContent& content);

/// The grid's spacing for a span of declination or right ascension and a density: the round step that draws about the
/// density's number of lines across the span.
///
/// @param spanDegrees the span, in degrees.
/// @param rightAscension whether the steps are of right ascension, in whole hours, minutes and seconds of time.
/// @param density the density.
/// @return the spacing in degrees.
[[nodiscard]] AFTERNIGHTCONTROLS_API double annotationGridSpacingDegrees(double spanDegrees, bool rightAscension,
                                                                         AnnotationGridDensity density) noexcept;

/// The round angular length a scale bar shows for a frame: the one nearest a sixth of the frame's width, from an
/// arcsecond to twenty degrees.
///
/// @param frameWidthArcseconds the sky the frame's width spans, in arcseconds.
/// @return the length in arcseconds; zero for a span that is not positive.
[[nodiscard]] AFTERNIGHTCONTROLS_API double annotationScaleBarArcseconds(double frameWidthArcseconds) noexcept;

/// An angle's text as a scale bar labels it: `30″`, `5′`, `1.5°`.
///
/// @param arcseconds the angle.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString annotationAngleText(double arcseconds);

} // namespace an::ui
