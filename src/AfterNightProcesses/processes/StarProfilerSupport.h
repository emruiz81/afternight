// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the two star-profiler windows share: one detector behind both layouts, the stars' world coordinates and
/// catalog matches, the figures each layout's table shows and orders by, and the star-profile documents each writes and
/// reads.
///
/// **One detector.** Both windows find their stars with the engine's fitted detector (`planStarDetection`,
/// `detectStars`), every representation read as fractions of its full scale, in a colour image's green channel, or its
/// CIE lightness when the second layout asks, and in a grey image's only channel. The first layout's dialog states its
/// controls in the detector's terms: the threshold is the smallest fitted amplitude above the sky, the roundness
/// tolerance holds the difference of the two fitted standard deviations, and the size window holds a star's mean full
/// width. The second layout's controls are the detector's own.
///
/// **One coordinate convention, converted nowhere.** A detected star's position is an engine pixel coordinate, the
/// first pixel's centre at one half. Both layouts write their documents in that convention, so a document is read and
/// written as it stands, with no half-pixel shift either way, and a star a window saves is read back where it was. A
/// world coordinate is the solution's projection of that position.
///
/// **A star's catalog facts travel beside it.** The engine's detected star carries what the pixels said and nothing a
/// catalog adds, so the second layout keeps its stars' world coordinates and Gaia matches in a table keyed by each
/// star's position, which a filter or an ordering copies unchanged.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "controls/StarFilter.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>
#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

// ---- What each layout asks for.

/// The first layout's dialog, read on the GUI thread when a detection is asked for.
struct FirstProfilerSettings
{
    /// The smallest fitted amplitude above the sky a star may have, as a fraction of full scale.
    double detectionThreshold = 0.1;

    /// The largest difference of a star's two fitted standard deviations, in pixels.
    double roundnessTolerance = 2.0;

    /// The smallest mean full width a star may have, in pixels.
    int minimumSize = 2;

    /// The largest mean full width a star may have, in pixels.
    int maximumSize = 25;
};

/// The second layout's dialog, read on the GUI thread when a detection is asked for.
struct SecondProfilerSettings
{
    /// Half the width of the box a candidate has to dominate, in pixels.
    int radius = 5;

    /// How many noise deviations the detection threshold stands above the sky, before the detector's factor of five.
    double sigma = 1.0;

    /// Whether the amplitude range applies.
    bool amplitudeRange = false;

    /// The smallest fitted amplitude a star may have, as a fraction of full scale, when the range applies.
    double minimumAmplitude = 0.0;

    /// The largest fitted amplitude a star may have, as a fraction of full scale, when the range applies.
    double maximumAmplitude = 1.0;

    /// The smallest axis ratio a star may have.
    double roundness = 0.5;

    /// Whether the roundness range applies.
    bool roundnessRange = false;

    /// The largest axis ratio a star may have, when the range applies.
    double maximumRoundness = 1.0;

    /// How many units of solver iteration each fit is allowed.
    int convergence = 1;

    /// Whether the scan's shape checks and the residual check are relaxed.
    bool relaxChecks = false;

    /// Whether the stars are fitted with a Moffat profile whose exponent is free, rather than a Gaussian.
    bool moffat = false;

    /// The smallest Moffat exponent a star may have.
    double minimumBeta = 1.5;

    /// Whether a colour image's stars are found in its CIE lightness rather than its green channel.
    bool useLightness = false;
};

/// The detector's request for the first layout: the detector's defaults - its search box among them, whatever the size
/// window - with the threshold as the smallest amplitude, nearly any axis ratio admitted and a Gaussian profile. The
/// size window and the roundness tolerance are held afterwards, by firstProfilerKeeps.
///
/// @param settings the dialog.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarDetectionRequest
firstProfilerDetection(const FirstProfilerSettings& settings) noexcept;

/// Whether the first layout keeps a detected star: its two fitted standard deviations within the roundness tolerance
/// of each other, and its mean full width inside the size window.
///
/// @param star the star.
/// @param settings the dialog.
/// @return true when the star is kept.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool firstProfilerKeeps(const an::DetectedStar& star,
                                                              const FirstProfilerSettings& settings) noexcept;

/// The detector's request for the second layout: its dialog, field for field.
///
/// @param settings the dialog.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarDetectionRequest
secondProfilerDetection(const SecondProfilerSettings& settings) noexcept;

// ---- A detection.

/// What one detection found.
struct ProfileDetection
{
    /// The stars, in the detector's order.
    an::ui::StarList stars;

    /// What the stars say about the frame, and the channel's statistics.
    an::StarFieldSummary summary;

    /// What the detector counted on the way.
    an::StarDetectionReport report;

    /// How many channels the image has.
    std::uint32_t channelCount = 1u;
};

/// Finds an image's stars with the engine's detector.
///
/// The image is copied and read as fractions of its representation's full scale, its validity honoured and a sample
/// that holds no number left out; the stars are found in a colour image's green channel, or in its CIE lightness when
/// asked, and in a grey image's only channel.
///
/// @param scope the run's admitted execution.
/// @param source the image, under the caller's lease.
/// @param request what the detector is asked.
/// @param useLightness whether a colour image's stars are found in its lightness.
/// @return the stars; or what copying, extracting, planning or detecting refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ProfileDetection>
detectProfileStars(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                   const an::StarDetectionRequest& request, bool useLightness);

// ---- What a catalog adds.

/// The Gaia entry a star matched.
struct StarCatalogMatch
{
    /// The entry's identifier.
    std::string identifier;

    /// Its G magnitude.
    double magnitudeG = 0.0;

    /// Its BP minus RP colour, when both bands were measured.
    std::optional<double> colorIndexBpMinusRp;

    /// How far the star lies from it on the sky, in arcseconds.
    double separationArcseconds = 0.0;
};

/// What the solution and the catalog say about one star.
struct StarEnrichment
{
    /// Where the solution places the star's position on the sky.
    std::optional<an::SkyCoordinate> direction;

    /// The catalog entry it matched, one to one.
    std::optional<StarCatalogMatch> match;
};

/// What the catalog says about a frame's stars as a whole.
struct ProfileCatalogFigures
{
    /// How many stars matched an entry.
    std::uint64_t matchedCount = 0u;

    /// The G zero point fitted over the matched stars' box sums.
    std::optional<double> zeroPointG;

    /// Its standard uncertainty.
    std::optional<double> zeroPointError;

    /// The faintest G magnitude a star matched.
    std::optional<double> limitingMagnitude;
};

/// What the catalog enrichment is asked to use.
struct ProfileEnrichmentRequest
{
    /// The installed Gaia catalog's directory; empty for none, which places the stars on the sky and matches nothing.
    QString catalogDirectory;

    /// The epoch the catalog's stars are carried to; nothing keeps the catalog's own.
    std::optional<double> observationEpochYear;

    /// How far a star may lie from an entry and still match it, in arcseconds.
    double matchRadiusArcseconds = 5.0;

    /// The faintest G magnitude an entry may have.
    double magnitudeLimit = 16.0;
};

/// What the enrichment found, one record per star in the list's order.
struct ProfileEnrichment
{
    /// One record per star.
    std::vector<StarEnrichment> stars;

    /// How many stars the solution placed on the sky.
    std::uint64_t placedCount = 0u;

    /// The catalog's figures.
    ProfileCatalogFigures figures;

    /// Why the catalog could not be read or matched, when it could not; the stars are still placed on the sky.
    std::optional<an::Status> catalogFailure;
};

/// Places each star on the sky through a solution and matches the stars one to one against the installed Gaia
/// catalog's stars inside the field, carried to the epoch: the matched stars' box sums fit the G zero point.
///
/// @param execution the run's admitted execution.
/// @param stars the stars, in engine pixel coordinates.
/// @param solution the image's solution.
/// @param request the catalog and the match's rule.
/// @return the enrichment, whose catalog failure says why nothing matched when the catalog refused; or what placing
///         the stars refused - a cancellation among them.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<ProfileEnrichment>
enrichProfileStars(const an::ExecutionContext& execution, std::span<const an::DetectedStar> stars,
                   const an::WcsSolution& solution, const ProfileEnrichmentRequest& request);

/// The key a star's enrichment is filed under: its position, which a filter and an ordering copy unchanged.
using StarPositionKey = std::pair<double, double>;

/// Each star's enrichment, by its position.
using StarEnrichmentTable = std::map<StarPositionKey, StarEnrichment>;

/// The key of a star.
///
/// @param star the star.
/// @return its position.
[[nodiscard]] AFTERNIGHTPROCESSES_API StarPositionKey starPositionKey(const an::DetectedStar& star) noexcept;

/// Files an enrichment under its stars' positions.
///
/// @param stars the stars the enrichment was made for.
/// @param enrichment one record per star, in the same order.
/// @return the table.
[[nodiscard]] AFTERNIGHTPROCESSES_API StarEnrichmentTable enrichmentTable(std::span<const an::DetectedStar> stars,
                                                                          const ProfileEnrichment& enrichment);

/// The enrichment of a star, or null when the table holds none.
///
/// @param table the table.
/// @param star the star.
/// @return its enrichment, valid while the table lives; null for none.
[[nodiscard]] AFTERNIGHTPROCESSES_API const StarEnrichment* enrichmentOf(const StarEnrichmentTable& table,
                                                                         const an::DetectedStar& star);

// ---- The tables.

/// The first layout's columns, in its table's order.
enum class FirstProfilerColumn : std::uint8_t
{
    /// The position across.
    X = 0u,
    /// The position down.
    Y,
    /// The fitted amplitude.
    Intensity,
    /// The share of its light the star was given, always one.
    Percentage,
    /// Half the difference of the two full widths.
    DeltaRadius,
    /// The brightness-weighted roundness figure.
    Quality,
    /// Half the mean full width.
    Radius,
    /// The radius times one and a half over 2.35, the first layout's relation of its two radii.
    MeanRadius,
    /// Half the major full width, on the larger side.
    LargeMajorAxis,
    /// Half the major full width, on the smaller side.
    SmallMajorAxis,
    /// Half the minor full width, on the larger side.
    LargeMinorAxis,
    /// Half the minor full width, on the smaller side.
    SmallMinorAxis,
    /// The major axis's position angle, in degrees.
    MajorAxisAngle
};

/// How many columns the first layout's table has.
inline constexpr int kFirstProfilerColumnCount = 13;

/// The second layout's columns, in its table's order.
enum class SecondProfilerColumn : std::uint8_t
{
    /// The position across.
    X = 0u,
    /// The position down.
    Y,
    /// The instrumental magnitude.
    Magnitude,
    /// The major full width.
    FullWidthX,
    /// The minor full width.
    FullWidthY,
    /// The right ascension the solution places the star at.
    RightAscension,
    /// The declination the solution places the star at.
    Declination,
    /// The major axis's position angle, in degrees.
    Angle,
    /// The fitted sky.
    Background,
    /// The fitted amplitude.
    Amplitude,
    /// The Moffat exponent.
    Beta,
    /// The minor full width over the major.
    Ratio,
    /// The fit's residual root mean square.
    ResidualRootMeanSquare,
    /// The mean full width.
    FullWidth,
    /// The brightness-weighted roundness figure.
    Circularity
};

/// How many columns the second layout's table has.
inline constexpr int kSecondProfilerColumnCount = 15;

/// A star's figure in a first-layout column.
///
/// @param star the star.
/// @param column the column.
/// @return the figure.
[[nodiscard]] AFTERNIGHTPROCESSES_API double firstProfilerFigure(const an::DetectedStar& star,
                                                                 FirstProfilerColumn column) noexcept;

/// A star's figure in a second-layout column; nothing where the star has none - a Gaussian's exponent, a magnitude it
/// was not given, a world coordinate the solution did not give it.
///
/// @param star the star.
/// @param enrichment its enrichment, or null.
/// @param column the column.
/// @return the figure, or nothing.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<double> secondProfilerFigure(const an::DetectedStar& star,
                                                                                 const StarEnrichment* enrichment,
                                                                                 SecondProfilerColumn column) noexcept;

/// The text a table cell shows for a figure: fixed to the column's decimals, "-" for nothing.
///
/// @param figure the figure.
/// @param decimals how many decimals.
/// @return the text.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString figureText(std::optional<double> figure, int decimals);

/// A list ordered by a first-layout column, stably: equal figures keep their order.
///
/// @param stars the list.
/// @param column the column.
/// @param descending largest first.
/// @return the ordered list.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ui::StarList
orderedForFirstProfiler(an::ui::StarList stars, FirstProfilerColumn column, bool descending);

/// A list ordered by a second-layout column, stably: equal figures keep their order and a star without the figure
/// comes after every one with it ascending and before them descending, the engine's rule.
///
/// @param stars the list.
/// @param enrichment the list's enrichment.
/// @param column the column.
/// @param descending largest first.
/// @return the ordered list.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ui::StarList orderedForSecondProfiler(an::ui::StarList stars,
                                                                                const StarEnrichmentTable& enrichment,
                                                                                SecondProfilerColumn column,
                                                                                bool descending);

// ---- The documents.

/// A first-generation document's header for a detection: the dialog it ran with and the frame's figures, with no star.
///
/// @param settings the dialog.
/// @param summary what the detection said about the frame.
/// @return the header.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarProfileV1 firstProfileHeader(const FirstProfilerSettings& settings,
                                                                           const an::StarFieldSummary& summary);

/// A second-generation document's header for a detection: the dialog it ran with, the frame's figures and the
/// catalog's, with no star.
///
/// @param settings the dialog.
/// @param summary what the detection said about the frame.
/// @param figures what the catalog said.
/// @param channelCount how many channels the image has.
/// @return the header.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarProfileV2 secondProfileHeader(const SecondProfilerSettings& settings,
                                                                            const an::StarFieldSummary& summary,
                                                                            const ProfileCatalogFigures& figures,
                                                                            std::uint32_t channelCount);

/// A first-generation star record of a detected star, as the first layout's figures state it. Its radius is the
/// radius a profile's reader paints a star of at its full width - twice the mean full width - so a star read back has
/// the width it was written with.
///
/// @param star the star.
/// @return the record, at the star's position.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarProfileV1Star firstProfileStar(const an::DetectedStar& star) noexcept;

/// A second-generation star record of a detected star and its enrichment, field for field.
///
/// @param star the star.
/// @param enrichment its enrichment, or null.
/// @return the record, at the star's position.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarProfileV2Star secondProfileStar(const an::DetectedStar& star,
                                                                              const StarEnrichment* enrichment);

/// The document a header and a list make.
///
/// @param header the header, of either generation; its stars are replaced.
/// @param stars the list.
/// @param enrichment the list's enrichment, which a second-generation record carries.
/// @param manuallyEdited whether a person changed the list after it was detected.
/// @return the document.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarProfileFile profileDocument(an::StarProfilePayload header,
                                                                          std::span<const an::DetectedStar> stars,
                                                                          const StarEnrichmentTable& enrichment,
                                                                          bool manuallyEdited);

/// What a window holds of a document it read.
struct LoadedProfile
{
    /// The document without its stars, which a save writes again.
    an::StarProfilePayload header;

    /// The stars, as detected-star records, in the document's order.
    an::ui::StarList stars;

    /// What a second-generation document recorded of each star's sky position and catalog match.
    StarEnrichmentTable enrichment;
};

/// What a window holds of a document: its header, its stars - read by the conversion every profile reader shares -
/// and, for the second generation, each star's recorded world coordinate and catalog match.
///
/// @param document the document.
/// @return what the window holds.
[[nodiscard]] AFTERNIGHTPROCESSES_API LoadedProfile loadedProfile(const an::StarProfileFile& document);

} // namespace an::ui::processes
