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
/// (`annotationDetailOf`, `annotationPaletteOf`). A person's own colours are kept apart
/// (`AnnotationStyle::customColors`), so choosing a palette and then the custom one again brings them back.
///
/// **A view takes three of them, and a text size of its own.** The live annotations of an image view offer a level of
/// detail, what a label says and a palette, named as a picture names them, and a text size: a picture's text follows
/// the picture, a view's the screen, as a multiple of the interface's captions (`AnnotationViewChoices`). They are
/// stored apart from a picture's choices.
#pragma once

#include "AfterNightControlsExport.h"

#include <QColor>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>
#include <span>

namespace an::ui
{

/// How much of the catalogues an annotation names.
enum class AnnotationDetail : std::uint8_t
{
    /// The Messier and Caldwell objects and the named ones, and the brightest stars by their proper names.
    Essential,

    /// Every catalogue to the twelfth magnitude and the objects large enough to show, and the stars to magnitude 4.5
    /// by their names and designations.
    Standard,

    /// The catalogues to the fourteenth magnitude and smaller objects, and the stars to the sixth, the unnamed ones
    /// marked.
    Detailed,

    /// Every galaxy, nebula and cluster the catalogues hold in the field, and every star of the bright-star catalogue,
    /// each by its name, its designation or its catalogue number.
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

    /// Icy blues, from cyan to periwinkle.
    Glacier,

    /// Violets and lavenders.
    Amethyst,

    /// Golds and warm yellows.
    Amber,

    /// Greens, from mint to lime.
    Verdant,

    /// Pinks and roses.
    Rose,

    /// The colours of a printed star atlas: red galaxies, green nebulae, yellow clusters.
    Atlas,

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

/// How large a view draws its annotations' text on the screen: its labels and its grid's.
enum class AnnotationViewTextSize : std::uint8_t
{
    /// As large as the interface's captions.
    Small,

    /// A quarter larger.
    Medium,

    /// Half as large again.
    Large
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

/// How heavy a picture's letters are.
enum class AnnotationFontWeight : std::uint8_t
{
    Light,
    Regular,
    Medium,
    Bold
};

/// Which stars a picture names, and by what.
enum class AnnotationStarNaming : std::uint8_t
{
    /// The stars that have a proper name, by it: `Sirius`, `Betelgeuse`.
    ProperNames,

    /// Those, and the stars that have a Bayer or Flamsteed designation: `α Ori`, `61 Cyg`.
    Designations,

    /// Every star: by its name or designation, else by its catalogue number, `HIP 27989`.
    CatalogueNumbers
};

/// How a star is marked.
enum class AnnotationStarMarker : std::uint8_t
{
    /// A ring around it.
    Ring,

    /// Four short ticks pointing at it, which leave the star itself clear.
    Reticle
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

    /// The bright stars.
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

/// Which bright stars a picture marks and names.
struct AnnotationStarSelection
{
    /// The faintest magnitude marked.
    double magnitudeLimit = 4.5;

    /// Which stars are named, and by what.
    AnnotationStarNaming naming = AnnotationStarNaming::Designations;

    /// Whether a star the naming gives no label is still marked, without one.
    bool markUnnamed = false;

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

/// How a picture looks. The text and the lines are set apart: no choice of one changes the other.
struct AnnotationStyle
{
    /// The colours.
    AnnotationColors colors{};

    /// The person's own colours, kept while a palette is chosen so the custom palette can be chosen again; none until
    /// the person makes them.
    std::optional<AnnotationColors> customColors{};

    // The text.

    /// The typeface of every text of the picture; empty takes the system's sans-serif face.
    QString fontFamily;

    /// How heavy the letters are; a designation and a title are drawn a step heavier than the rest.
    AnnotationFontWeight fontWeight = AnnotationFontWeight::Regular;

    /// Whether the objects' and stars' labels are slanted; the grid's labels and the panel stay upright.
    bool italicLabels = false;

    /// The labels' size, as a multiple of the default; from `kAnnotationMinimumTextSize` to
    /// `kAnnotationMaximumTextSize`.
    double textSize = 1.0;

    /// The panel's text size, as a multiple of the labels'; from `kAnnotationMinimumPanelSize` to
    /// `kAnnotationMaximumPanelSize`.
    double panelSize = 1.0;

    /// How a label stands out from the image.
    AnnotationLabelBacking labelBacking = AnnotationLabelBacking::Halo;

    /// Whether a label takes its object's colour rather than the text colour.
    bool labelsInObjectColor = true;

    // The lines.

    /// The lines' width, as a multiple of the default; from `kAnnotationMinimumLineWidth` to
    /// `kAnnotationMaximumLineWidth`.
    double lineWidth = 1.0;

    /// How opaque the objects' and stars' lines and the leader lines are, from `kAnnotationMinimumLineOpacity` to one.
    double lineOpacity = 1.0;

    /// Whether a soft dark edge in the halo colour is drawn behind the lines, so they read over a bright image.
    bool lineHalo = false;

    /// How a deep-sky object is marked.
    AnnotationMarker marker = AnnotationMarker::Outline;

    /// Whether an outlined object is filled with its colour; an object whose outline spans much of the frame is not.
    bool fillObjects = false;

    /// How opaque that fill is, from zero to one.
    double fillOpacity = 0.15;

    /// How a star is marked.
    AnnotationStarMarker starMarker = AnnotationStarMarker::Ring;

    /// How opaque the grid's lines are, from zero to one.
    double gridOpacity = 0.45;

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

/// What a view's live annotations name and how they look: the choices of an annotated picture its menu offers - a level
/// of detail, what a label says and a palette - and the size its text is drawn at on the screen. A view offers no
/// custom level or palette.
struct AnnotationViewChoices
{
    /// How much of the catalogues the view names; never `AnnotationDetail::Custom`.
    AnnotationDetail detail = AnnotationDetail::Standard;

    /// What an object's label says.
    AnnotationLabelText labelText = AnnotationLabelText::Designation;

    /// The colours the annotations and the grid are drawn in; never `AnnotationPalette::Custom`.
    AnnotationPalette palette = AnnotationPalette::Aurora;

    /// How large the labels and the grid's labels are drawn on the screen.
    AnnotationViewTextSize textSize = AnnotationViewTextSize::Medium;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const AnnotationViewChoices& left,
                                         const AnnotationViewChoices& right) noexcept = default;
};

/// A magnitude limit that marks every magnitude.
inline constexpr double kAnnotationNoMagnitudeLimit = 99.0;

/// The faintest magnitude the bundled bright-star catalogue holds.
inline constexpr double kAnnotationFaintestStar = 9.0;

/// The smallest and largest text size a style takes.
inline constexpr double kAnnotationMinimumTextSize = 0.5;
inline constexpr double kAnnotationMaximumTextSize = 2.5;

/// The smallest and largest panel text a style takes, as multiples of the labels' size.
inline constexpr double kAnnotationMinimumPanelSize = 0.5;
inline constexpr double kAnnotationMaximumPanelSize = 2.0;

/// The thinnest and widest line a style takes.
inline constexpr double kAnnotationMinimumLineWidth = 0.5;
inline constexpr double kAnnotationMaximumLineWidth = 3.0;

/// The faintest lines a style takes: a line is never drawn invisible.
inline constexpr double kAnnotationMinimumLineOpacity = 0.1;

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

/// The levels of detail a person chooses from, in the order they are offered.
///
/// @return every level but `AnnotationDetail::Custom`.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::span<const AnnotationDetail> annotationDetails() noexcept;

/// A level of detail's name, as a person chooses it: `Essential`, `Standard`, `Detailed`, `Complete`.
///
/// @param detail the level; `AnnotationDetail::Custom` is named `Custom`.
/// @return the name.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString annotationDetailName(AnnotationDetail detail);

/// What a level of detail names, in a sentence a person reads beside its name.
///
/// @param detail the level.
/// @return the sentence; empty for `AnnotationDetail::Custom`.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString annotationDetailDescription(AnnotationDetail detail);

/// The level of detail that names about what a view named at a magnitude limit, for a view that kept a limit before it
/// offered levels: the notable objects alone to the sixth magnitude, the standard level to the twelfth, the detailed
/// level to the fourteenth and every object at a fainter limit.
///
/// @param limit the magnitude limit.
/// @return the level; the standard one for a limit that is no number.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationDetail annotationDetailOfMagnitudeLimit(double limit) noexcept;

/// The palettes a person chooses from, in the order they are offered.
///
/// @return every palette but `AnnotationPalette::Custom`.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::span<const AnnotationPalette> annotationPalettes() noexcept;

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

/// A palette's name, as a person chooses it: `Aurora`, `Ember - for dark-adapted eyes`.
///
/// @param palette the palette; `AnnotationPalette::Custom` is named `Custom`.
/// @return the name.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString annotationPaletteName(AnnotationPalette palette);

/// The text sizes a view offers, in the order they are offered: small, medium and large.
///
/// @return the sizes.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::span<const AnnotationViewTextSize> annotationViewTextSizes() noexcept;

/// A view's text size's name, as a person chooses it: `Small`, `Medium`, `Large`.
///
/// @param size the size.
/// @return the name.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString annotationViewTextSizeName(AnnotationViewTextSize size);

/// How large a view draws its annotations' text at a size, as a multiple of the interface's captions.
///
/// @param size the size.
/// @return the multiple: one for the small size, larger for each larger size.
[[nodiscard]] AFTERNIGHTCONTROLS_API double annotationViewTextScale(AnnotationViewTextSize size) noexcept;

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

/// The text a view's choices are stored as: a JSON object naming each choice.
///
/// @param choices the choices; a custom level or palette is stored as the default.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString storedAnnotationViewChoices(const AnnotationViewChoices& choices);

/// The view's choices stored text names. Each choice is read through a fixed table; one that is missing, or names
/// nothing a view offers - a custom level or palette among them - reads as its default.
///
/// @param stored the stored text.
/// @return the choices.
[[nodiscard]] AFTERNIGHTCONTROLS_API AnnotationViewChoices annotationViewChoicesFromStored(const QString& stored);

} // namespace an::ui
