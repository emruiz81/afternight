// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an annotated picture of a solved image shows and how it looks: the layers, how much of the bundled catalogues
/// they name, what a label says, the colours and the sizes, the corners the panel, the compass and the scale bar take,
/// and the picture's size, stretch and background - with the presets a person starts from and the text the choices
/// are stored as.
///
/// **Every length is a fraction of the image.** A label's height, a line's width, a marker's radius and the margin of
/// the decorations are fractions of the image's reference length, the geometric mean of its width and height
/// (`annotationReferenceLength`), so the picture made at full size, a picture made smaller and the window's preview are
/// one drawing at different scales.
///
/// **The colours are the picture's, not the theme's.** They are part of what a person makes and are kept with the
/// other choices, so a picture looks the same whichever theme the application shows. The window that edits them draws
/// itself through the design tokens, as every window does.
///
/// **A preset is a set of values, not a mode.** Choosing a level of detail or a palette writes its values into the
/// choices; changing one of them afterwards leaves the others as they are, and the level or palette reads as custom
/// (`annotationDetailOf`, `annotationPaletteOf`).
#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QSize>
#include <QString>

#include <cstdint>

namespace an::ui
{

/// How much of the catalogues an annotation names.
enum class AnnotationDetail : std::uint8_t
{
    /// The Messier and Caldwell objects and the named ones, and the brightest named stars.
    Essential,

    /// Every catalogue to the twelfth magnitude and the objects large enough to show, and the stars to the fourth.
    Standard,

    /// The catalogues to the fourteenth magnitude and smaller objects, and the stars to magnitude 5.5.
    Detailed,

    /// Every galaxy, nebula and cluster the catalogues hold in the field, and every bright star.
    Complete,

    /// Choices no level makes.
    Custom
};

/// The colours a picture starts from.
enum class AnnotationPalette : std::uint8_t
{
    /// A colour for each kind of object over a soft blue grid.
    Aurora,

    /// White lines and labels, for a picture that stays quiet.
    Moonlight,

    /// Reds, for a picture read beside a dark-adapted eye.
    Ember,

    /// Dark lines and labels with a light halo, for a bright image or print.
    Ink,

    /// Colours no palette has.
    Custom
};

/// What an object's label says.
enum class AnnotationLabelText : std::uint8_t
{
    /// Its designation: `M 42`, `NGC 3372`.
    Designation,

    /// Its common name, or its designation when it has none.
    Name,

    /// Its designation, with its common name on a second line when it has one.
    DesignationAndName
};

/// How far apart the equatorial grid's lines are drawn.
enum class AnnotationGridDensity : std::uint8_t
{
    /// About four lines across the field.
    Coarse,

    /// About six.
    Normal,

    /// About ten.
    Fine
};

/// How a label stands out from the image under it.
enum class AnnotationLabelBacking : std::uint8_t
{
    /// A soft outline around each letter.
    Halo,

    /// A rounded box behind the label.
    Badge,

    /// The letters alone.
    Plain
};

/// How a deep-sky object is marked.
enum class AnnotationMarker : std::uint8_t
{
    /// Its outline: an ellipse of its size at its position angle, or a circle for an object of no stated size.
    Outline,

    /// Four corner brackets around where it lies.
    Brackets
};

/// A corner of the picture.
enum class AnnotationCorner : std::uint8_t
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

/// What the picture is drawn over.
enum class AnnotationBackground : std::uint8_t
{
    /// The image, presented as the stretch says.
    Image,

    /// Nothing: the annotations alone over transparent pixels, to lay over the image elsewhere.
    Transparent
};

/// How the image under the annotations is presented.
enum class AnnotationStretch : std::uint8_t
{
    /// As the view shows it: its stretch, its colour balance and its channel linking.
    AsShown,

    /// As its samples are, every channel at full scale and nothing stretched.
    AsStored
};

/// The layers a picture draws.
struct AnnotationLayers
{
    /// The equatorial grid and its labels at the picture's edges.
    bool grid = true;

    /// The galaxies, nebulae and clusters.
    bool deepSky = true;

    /// The bright stars' names.
    bool stars = true;

    /// The arrows to celestial north and east.
    bool compass = true;

    /// A bar of a round angular length.
    bool scaleBar = true;

    /// The title, the credit and the facts of the image.
    bool infoPanel = true;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationLayers& left,
                                         const AnnotationLayers& right) noexcept = default;
};

/// Which deep-sky objects a picture marks.
struct AnnotationDeepSkySelection
{
    /// Whether Messier objects are marked.
    bool messier = true;

    /// Whether Caldwell objects are marked.
    bool caldwell = true;

    /// Whether NGC objects are marked.
    bool ngc = true;

    /// Whether IC objects are marked.
    bool ic = true;

    /// Whether an NGC or IC object is marked only when it has a common name.
    bool namedOnly = false;

    /// The faintest magnitude marked; `kAnnotationNoMagnitudeLimit` or more marks every magnitude.
    double magnitudeLimit = 12.0;

    /// Whether an object no catalogue measured a magnitude for is marked; a named one always is.
    bool unmeasured = false;

    /// The smallest major axis marked, as a fraction of the reference length; a Messier, Caldwell or named object is
    /// marked whatever its size, and an object of no stated size is judged by its magnitude alone.
    double minimumSize = 0.003;

    /// Whether galaxies are marked.
    bool galaxies = true;

    /// Whether bright and dark nebulae and clusters with nebulosity are marked.
    bool nebulae = true;

    /// Whether planetary nebulae are marked.
    bool planetaryNebulae = true;

    /// Whether open clusters are marked.
    bool openClusters = true;

    /// Whether globular clusters are marked.
    bool globularClusters = true;

    /// Whether the catalogues' other entries - stars, asterisms, knots and entries of no known kind - are marked.
    bool otherObjects = false;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationDeepSkySelection& left,
                                         const AnnotationDeepSkySelection& right) noexcept = default;
};

/// Which bright stars a picture names.
struct AnnotationStarSelection
{
    /// The faintest magnitude named.
    double magnitudeLimit = 4.0;

    /// Whether a star with no proper name is named by its Bayer letter.
    bool bayerLetters = true;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationStarSelection& left,
                                         const AnnotationStarSelection& right) noexcept = default;
};

/// Which facts of the image the panel states, each when the image states it.
struct AnnotationPanelFacts
{
    /// Where the centre of the field points.
    bool coordinates = true;

    /// The field's size, its pixel scale and the orientation of north.
    bool field = true;

    /// The telescope and its focal length.
    bool optics = true;

    /// The camera.
    bool camera = true;

    /// The filter.
    bool filter = true;

    /// The date the observation began.
    bool date = true;

    /// The integration and the frames it holds.
    bool integration = true;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationPanelFacts& left,
                                         const AnnotationPanelFacts& right) noexcept = default;
};

/// What a picture shows.
struct AnnotationContent
{
    /// The layers drawn.
    AnnotationLayers layers{};

    /// The deep-sky objects marked.
    AnnotationDeepSkySelection deepSky{};

    /// The stars named.
    AnnotationStarSelection stars{};

    /// What an object's label says.
    AnnotationLabelText labelText = AnnotationLabelText::DesignationAndName;

    /// How far apart the grid's lines are.
    AnnotationGridDensity gridDensity = AnnotationGridDensity::Normal;

    /// Whether the grid's lines are labelled where they meet the picture's edge.
    bool gridLabels = true;

    /// The facts the panel states.
    AnnotationPanelFacts facts{};

    /// The panel's title; empty takes the target the image's cards name, or else the most prominent named object.
    QString title;

    /// A line under the title, such as the author's name.
    QString credit;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationContent& left, const AnnotationContent& right) = default;
};

/// The colours of a picture. A colour's alpha is its opacity.
struct AnnotationColors
{
    /// The equatorial grid's lines and labels; drawn at the style's grid opacity.
    QColor grid;

    /// Galaxies.
    QColor galaxy;

    /// Bright and dark nebulae and clusters with nebulosity.
    QColor nebula;

    /// Planetary nebulae.
    QColor planetaryNebula;

    /// Open clusters.
    QColor openCluster;

    /// Globular clusters.
    QColor globularCluster;

    /// The catalogues' other entries.
    QColor otherObject;

    /// The bright stars.
    QColor star;

    /// A label's letters when labels do not take their object's colour.
    QColor text;

    /// The outline behind letters and lines that keeps them readable over the image.
    QColor halo;

    /// The compass and the scale bar.
    QColor decoration;

    /// The panel's and a badge's fill.
    QColor panel;

    /// The panel's text.
    QColor panelText;

    /// The panel's title and its rule.
    QColor accent;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationColors& left, const AnnotationColors& right) = default;
};

/// How a picture looks.
struct AnnotationStyle
{
    /// The colours.
    AnnotationColors colors{};

    /// The labels' size, as a multiple of the default; from `kAnnotationMinimumTextSize` to
    /// `kAnnotationMaximumTextSize`.
    double textSize = 1.0;

    /// The lines' width, as a multiple of the default; from `kAnnotationMinimumLineWidth` to
    /// `kAnnotationMaximumLineWidth`.
    double lineWidth = 1.0;

    /// How opaque the grid's lines are, from zero to one.
    double gridOpacity = 0.45;

    /// The labels' typeface; empty takes the system's sans-serif face.
    QString fontFamily;

    /// How a label stands out from the image.
    AnnotationLabelBacking labelBacking = AnnotationLabelBacking::Halo;

    /// How a deep-sky object is marked.
    AnnotationMarker marker = AnnotationMarker::Outline;

    /// Whether a label takes its object's colour rather than the text colour.
    bool labelsInObjectColor = true;

    /// Whether a label that cannot sit beside its object may sit further away, joined to it by a line.
    bool leaderLines = true;

    /// Whether an object whose label found no room is still marked.
    bool unlabelledMarks = true;

    /// The panel's corner.
    AnnotationCorner panelCorner = AnnotationCorner::BottomLeft;

    /// The compass's corner.
    AnnotationCorner compassCorner = AnnotationCorner::TopRight;

    /// The scale bar's corner.
    AnnotationCorner scaleBarCorner = AnnotationCorner::BottomRight;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationStyle& left, const AnnotationStyle& right) = default;
};

/// The picture made.
struct AnnotationOutput
{
    /// The picture's longer side in pixels; zero, or a side at least the image's, keeps the image's size. A picture is
    /// never larger than its image.
    std::uint32_t longEdge = 0u;

    /// How the image under the annotations is presented.
    AnnotationStretch stretch = AnnotationStretch::AsShown;

    /// What the annotations are drawn over.
    AnnotationBackground background = AnnotationBackground::Image;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationOutput& left,
                                         const AnnotationOutput& right) noexcept = default;
};

/// Every choice of an annotated picture.
struct AnnotationSettings
{
    AnnotationContent content{};
    AnnotationStyle style{};
    AnnotationOutput output{};

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationSettings& left, const AnnotationSettings& right) = default;
};

/// A magnitude limit that marks every magnitude.
inline constexpr double kAnnotationNoMagnitudeLimit = 99.0;

/// The faintest magnitude the bundled bright-star catalogue holds.
inline constexpr double kAnnotationFaintestStar = 6.5;

/// The smallest and largest text size a style takes.
inline constexpr double kAnnotationMinimumTextSize = 0.5;
inline constexpr double kAnnotationMaximumTextSize = 2.5;

/// The thinnest and widest line a style takes.
inline constexpr double kAnnotationMinimumLineWidth = 0.5;
inline constexpr double kAnnotationMaximumLineWidth = 3.0;

/// The default choices: the standard level of detail in the Aurora palette, at the image's size and as the view shows
/// it.
///
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationSettings defaultAnnotationSettings();

/// Writes a level of detail's values into the choices of what is shown: the deep-sky selection and the star selection.
/// The layers, the labels, the grid and the panel are left as they are; `AnnotationDetail::Custom` changes nothing.
///
/// @param content the choices.
/// @param detail the level.
AFTERNIGHTCONTROLS_API void applyAnnotationDetail(AnnotationContent& content, AnnotationDetail detail);

/// The level of detail whose values the choices hold.
///
/// @param content the choices.
/// @return the level, or `AnnotationDetail::Custom` when no level's values are the choices'.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationDetail annotationDetailOf(const AnnotationContent& content);

/// A palette's colours.
///
/// @param palette the palette; `AnnotationPalette::Custom` gives the Aurora palette's.
/// @return the colours.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationColors annotationPaletteColors(AnnotationPalette palette);

/// The palette whose colours these are.
///
/// @param colors the colours.
/// @return the palette, or `AnnotationPalette::Custom` when no palette's colours are these.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationPalette annotationPaletteOf(const AnnotationColors& colors);

/// The length every size of a picture is a fraction of: the geometric mean of the image's width and height.
///
/// @param imageSize the image's size.
/// @return the length in the image's pixels; zero for an empty size.
[[nodiscard]] AFTERNIGHTCONTROLS_API double annotationReferenceLength(QSize imageSize) noexcept;

/// The size of the picture an output makes of an image: the image's size scaled so its longer side is the output's
/// long edge, each side at least one pixel; the image's own size when the long edge is zero or not shorter.
///
/// @param imageSize the image's size.
/// @param longEdge the output's long edge.
/// @return the picture's size.
[[nodiscard]] AFTERNIGHTCONTROLS_API QSize annotationPictureSize(QSize imageSize, std::uint32_t longEdge) noexcept;

/// The text choices are stored as: a JSON object naming each choice. The title belongs to one image and is not stored.
///
/// @param settings the choices.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedAnnotationSettings(const AnnotationSettings& settings);

/// The choices stored text names. Each choice is read through a fixed table or held to its range; a choice that is
/// missing, of another kind or out of range reads as its default, so text from another version reads as far as it
/// goes.
///
/// @param stored the stored text.
/// @return the choices.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationSettings annotationSettingsFromStored(const QString& stored);

} // namespace an::ui
