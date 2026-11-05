// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Bounded records and text conversion for persisted star profiles.
///
/// A frame's detected stars are persisted as a JSON document in one of two schemas: version one,
/// the geometric profile of a DeepSkyStacker-derived detector, and version two, the fitted
/// point-spread functions, photometry and catalogue associations of a profiler that fits them. This
/// header carries every field either schema persists as a typed record, so a
/// consumer can read, change and write a profile without the JSON library and without losing a
/// field. `IO/StarProfileFile.h` reads and publishes such documents as files; this header is the
/// text half.
///
/// **The schema comes from the document, not the file name.** `parseStarProfileDocument` reads the
/// generation from the `version` and `engine` members of the document's `stars_profile` object and
/// refuses a combination it does not know. A version-one field the document leaves out reads as
/// zero rather than as the member's default; version two refuses a document missing any parameter
/// or frame-level field.
///
/// **Every input is bounded.** `StarProfileReadLimits` caps the document's bytes, the star count and
/// each string, on read and on write, so a crafted document is refused as `ErrorCode::LimitExceeded`
/// rather than becoming an allocation. The JSON library's values and exceptions never cross this
/// header.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace an
{

/// Which persisted star-profiler schema a document carries.
enum class StarProfileGeneration : std::uint8_t
{
    /// The geometric profile written by the first profiler, which derives from DeepSkyStacker's
    /// star detection.
    Version1 = 0u,

    /// The fitted point-spread-function and photometry profile written by the second profiler.
    Version2 = 1u
};

/// The analytic profile name stored by the version-two schema.
///
/// A document whose spelling is none of the three reads as `Gaussian`.
enum class PersistedPsfModel : std::uint8_t
{
    /// `"gaussian"`.
    Gaussian = 0u,

    /// `"moffat_bfree"`.
    MoffatFreeBeta = 1u,

    /// `"moffat_bfixed"`.
    MoffatFixedBeta = 2u
};

/// Detection inputs stored by a version-one document.
///
/// The member defaults are for a record built in memory; a read fills a field the document leaves
/// out with zero, as it does every version-one field.
struct StarProfileV1Parameters
{
    /// Candidate threshold in the detector's image domain.
    double detectionThreshold = 0.1;

    /// Maximum accepted axis ratio.
    double roundnessTolerance = 2.0;

    /// Smallest accepted star size, in pixels.
    double minimumSize = 2.0;

    /// Largest accepted star size, in pixels.
    double maximumSize = 25.0;
};

/// Frame figures stored by a version-one document.
struct StarProfileV1Global
{
    /// Frame full width at half maximum, in pixels.
    double fullWidth = 0.0;

    /// The detector's frame-quality figure.
    double quality = 0.0;

    /// Frame background in the source numeric domain.
    double background = 0.0;

    /// Frame background noise in the source numeric domain.
    double backgroundNoise = 0.0;
};

/// One version-one geometric star record.
struct StarProfileV1Star
{
    /// Integrated intensity in the detector's numeric domain.
    double intensity = 0.0;

    /// Percentage of the local brightness assigned to this star.
    double percentage = 0.0;

    /// Difference between the detector's two radius estimates, in pixels.
    double deltaRadius = 0.0;

    /// The detector's star-quality figure.
    double quality = 0.0;

    /// The detector's mean radius, in pixels.
    double meanRadius = 0.0;

    /// Full-width-derived radius, in pixels.
    double radius = 0.0;

    /// Horizontal position in the version-one schema's pixel coordinates.
    double x = 0.0;

    /// Vertical position in the version-one schema's pixel coordinates.
    double y = 0.0;

    /// Larger fitted major-axis radius, in pixels.
    double largeMajorAxis = 0.0;

    /// Smaller fitted major-axis radius, in pixels.
    double smallMajorAxis = 0.0;

    /// Larger fitted minor-axis radius, in pixels.
    double largeMinorAxis = 0.0;

    /// Smaller fitted minor-axis radius, in pixels.
    double smallMinorAxis = 0.0;

    /// Major-axis angle in the version-one schema's convention, in degrees.
    double majorAxisAngle = 0.0;
};

/// A complete version-one profile.
struct StarProfileV1
{
    /// Detection inputs that produced the record.
    StarProfileV1Parameters parameters{};

    /// Frame-level figures.
    StarProfileV1Global global{};

    /// Whether a consumer changed the star list after detection.
    bool manuallyEdited = false;

    /// Stars in persisted order; bounded by `StarProfileReadLimits::maximumStars` on input.
    std::vector<StarProfileV1Star> stars{};
};

/// Detection inputs stored by a version-two document.
struct StarProfileV2Parameters
{
    /// Half-width of the candidate box, in pixels.
    std::int32_t radius = 5;

    /// Detection-noise multiplier.
    double sigma = 1.0;

    /// Minimum accepted fitted roundness.
    double roundness = 0.5;

    /// Focal length used for angular widths, in millimetres.
    double focalLength = 0.0;

    /// Pixel pitch used for angular widths, in micrometres.
    double pixelSizeX = 0.0;

    /// The profiler's fit-convergence multiplier.
    std::int32_t convergence = 1;

    /// Whether the profiler's optional fit refusals were relaxed.
    bool relaxChecks = false;

    /// Analytic profile fitted to candidates.
    PersistedPsfModel profile = PersistedPsfModel::Gaussian;

    /// Smallest accepted Moffat exponent.
    double minimumBeta = 1.5;

    /// Smallest accepted fitted amplitude.
    double minimumAmplitude = 0.0;

    /// Largest accepted fitted amplitude; zero keeps the profiler's automatic ceiling.
    double maximumAmplitude = 0.0;

    /// Largest accepted roundness parameter.
    double maximumRoundness = 1.0;
};

/// Frame figures stored by a version-two document.
struct StarProfileV2Global
{
    /// Median fitted full width, in pixels.
    double fullWidth = 0.0;

    /// Median fitted roundness.
    double roundness = 0.0;

    /// The profiler's frame-quality figure.
    double quality = 0.0;

    /// Median star signal-to-noise.
    double signalToNoise = 0.0;

    /// Detection-channel background.
    double background = 0.0;

    /// Detection-channel background noise.
    double backgroundNoise = 0.0;

    /// Detection-channel median absolute deviation.
    double medianAbsoluteDeviation = 0.0;

    /// Detection-channel robust scale.
    double robustScale = 0.0;

    /// Calibrated Gaia G zero point.
    double zeroPointG = 0.0;

    /// Uncertainty of `zeroPointG`.
    double zeroPointError = 0.0;

    /// Number of stars associated with catalogue records.
    std::uint64_t catalogMatchedCount = 0u;

    /// Faintest reliable calibrated magnitude.
    double limitingMagnitude = 0.0;

    /// Number of source channels the profile reports; the arrays below retain the first three.
    std::uint32_t channelCount = 1u;

    /// Per-channel background noise in R/mono, G, B order.
    std::array<double, 3u> channelBackgroundNoise{};

    /// Per-channel median absolute deviation in R/mono, G, B order.
    std::array<double, 3u> channelMedianAbsoluteDeviation{};

    /// Per-channel robust scale in R/mono, G, B order.
    std::array<double, 3u> channelRobustScale{};
};

/// Fitted parameters stored for one version-two star.
struct StarProfileV2Psf
{
    /// Local background, stored as `B`.
    double background = 0.0;

    /// Fitted amplitude, stored as `A`.
    double amplitude = 0.0;

    /// Horizontal centre inside the fitted box.
    double centreX = 0.0;

    /// Vertical centre inside the fitted box.
    double centreY = 0.0;

    /// Major-axis fitted scale, in pixels.
    double scaleX = 0.0;

    /// Minor-axis fitted scale, in pixels.
    double scaleY = 0.0;

    /// Major-axis full width, in pixels.
    double fullWidthX = 0.0;

    /// Minor-axis full width, in pixels.
    double fullWidthY = 0.0;

    /// Major-axis full width, in arcseconds.
    double fullWidthXArcseconds = 0.0;

    /// Minor-axis full width, in arcseconds.
    double fullWidthYArcseconds = 0.0;

    /// Major-axis angle in degrees.
    double angle = 0.0;

    /// Root mean square of the fitted residual.
    double residualRootMeanSquare = 0.0;

    /// Saturation level in the source numeric domain.
    double saturation = 0.0;

    /// Half-width of the fitted box, in pixels.
    std::int32_t boxRadius = 0;

    /// Whether the star contained saturated samples.
    bool saturated = false;

    /// Fitted or held Moffat exponent.
    double beta = 0.0;

    /// Analytic profile fitted to the star.
    PersistedPsfModel profile = PersistedPsfModel::Gaussian;
};

/// Image position stored for one version-two star.
struct StarProfileV2Position
{
    /// Horizontal source position, in pixels.
    double x = 0.0;

    /// Vertical source position, in pixels.
    double y = 0.0;
};

/// Photometry stored for one version-two star.
struct StarProfileV2Photometry
{
    /// Instrumental magnitude.
    double magnitude = 0.0;

    /// Instrumental background magnitude.
    double backgroundMagnitude = 0.0;

    /// Uncertainty of `magnitude`.
    double magnitudeError = 0.0;

    /// Uncertainty of `backgroundMagnitude`.
    double backgroundMagnitudeError = 0.0;

    /// Signal-to-noise ratio.
    double signalToNoise = 0.0;

    /// Whether the photometry passed the profiler's checks.
    bool valid = false;

    /// The profiler's B-V colour index.
    double colorIndexBv = 0.0;
};

/// Relative fit uncertainties stored for one version-two star.
struct StarProfileV2Uncertainty
{
    /// Background uncertainty.
    double background = 0.0;

    /// Amplitude uncertainty.
    double amplitude = 0.0;

    /// Horizontal-centre uncertainty.
    double centreX = 0.0;

    /// Vertical-centre uncertainty.
    double centreY = 0.0;

    /// Major-scale uncertainty.
    double scaleX = 0.0;

    /// Minor-scale uncertainty.
    double scaleY = 0.0;

    /// Angle uncertainty.
    double angle = 0.0;

    /// Moffat-exponent uncertainty.
    double beta = 0.0;
};

/// Astrometry and catalogue association stored for one version-two star.
struct StarProfileV2Astrometry
{
    /// Source channel index.
    std::int32_t channel = 0;

    /// Coordinate-unit spelling, bounded by `StarProfileReadLimits::maximumStringBytes` on input.
    std::string units = "px";

    /// Right ascension in degrees.
    double rightAscension = 0.0;

    /// Declination in degrees.
    double declination = 0.0;

    /// Whether the world coordinates are present.
    bool hasWorldCoordinates = false;

    /// Catalogue identifier, bounded by `StarProfileReadLimits::maximumStringBytes` on input.
    std::string catalogId{};

    /// Catalogue Gaia G magnitude.
    double catalogMagnitudeG = 0.0;

    /// Catalogue Gaia BP magnitude.
    double catalogMagnitudeBp = 0.0;

    /// Catalogue Gaia RP magnitude.
    double catalogMagnitudeRp = 0.0;

    /// Catalogue BP-RP colour index.
    double colorIndexBpRp = 0.0;

    /// Angular association distance, in arcseconds.
    double matchSeparationArcseconds = 0.0;

    /// Whether the catalogue association is present.
    bool hasCatalogMatch = false;
};

/// Derived figures stored for one version-two star.
struct StarProfileV2Metrics
{
    /// Fitted-axis roundness.
    double roundness = 0.0;

    /// The profiler's circularity figure.
    double circularity = 0.0;

    /// Mean fitted full width, in pixels.
    double fullWidth = 0.0;
};

/// One complete version-two star record.
struct StarProfileV2Star
{
    /// Caller-visible name, bounded by `StarProfileReadLimits::maximumStringBytes` on input.
    std::string name{};

    /// Fitted distribution.
    StarProfileV2Psf psf{};

    /// Source position.
    StarProfileV2Position position{};

    /// Photometric figures.
    StarProfileV2Photometry photometry{};

    /// Fit uncertainties.
    StarProfileV2Uncertainty uncertainty{};

    /// World position and catalogue association.
    StarProfileV2Astrometry astrometry{};

    /// Derived shape figures.
    StarProfileV2Metrics metrics{};
};

/// A complete version-two profile.
struct StarProfileV2
{
    /// Detection inputs that produced the record.
    StarProfileV2Parameters parameters{};

    /// Frame-level figures.
    StarProfileV2Global global{};

    /// Whether a consumer changed the star list after detection.
    ///
    /// The version-two schema has no member for it, so a read defaults it to false and a write cannot
    /// persist true.
    bool manuallyEdited = false;

    /// Stars in persisted order; bounded by `StarProfileReadLimits::maximumStars` on input.
    std::vector<StarProfileV2Star> stars{};
};

/// The version-specific payload of a persisted profile.
using StarProfilePayload = std::variant<StarProfileV1, StarProfileV2>;

/// A profile whose schema was detected from its document rather than its filename.
struct StarProfileFile
{
    /// Detected schema generation. A parsed record always agrees with the active alternative in
    /// `payload`, and `writeStarProfileDocument` refuses one that does not.
    StarProfileGeneration generation = StarProfileGeneration::Version1;

    /// Version-specific fields.
    StarProfilePayload payload = StarProfileV1{};
};

/// Resource bounds applied before and during profile import.
struct StarProfileReadLimits
{
    /// Largest input document, in bytes.
    std::uint64_t maximumFileBytes = 64u * 1024u * 1024u;

    /// Largest accepted star array.
    std::uint32_t maximumStars = 1000000u;

    /// Largest accepted name, unit or catalogue identifier, in UTF-8 bytes.
    std::uint32_t maximumStringBytes = 4096u;
};

/// Fields by which a version-one star list can be ordered.
enum class StarProfileV1SortKey : std::uint8_t
{
    /// `StarProfileV1Star::intensity`.
    Intensity = 0u,
    /// `StarProfileV1Star::percentage`.
    Percentage,
    /// `StarProfileV1Star::deltaRadius`.
    DeltaRadius,
    /// `StarProfileV1Star::quality`.
    Quality,
    /// `StarProfileV1Star::meanRadius`.
    MeanRadius,
    /// `StarProfileV1Star::radius`.
    Radius,
    /// `StarProfileV1Star::x`.
    PositionX,
    /// `StarProfileV1Star::y`.
    PositionY,
    /// `StarProfileV1Star::largeMajorAxis`.
    LargeMajorAxis,
    /// `StarProfileV1Star::smallMajorAxis`.
    SmallMajorAxis,
    /// `StarProfileV1Star::largeMinorAxis`.
    LargeMinorAxis,
    /// `StarProfileV1Star::smallMinorAxis`.
    SmallMinorAxis,
    /// `StarProfileV1Star::majorAxisAngle`; deliberately last for range checking.
    MajorAxisAngle
};

/// Which way a star list is ordered.
enum class StarProfileSortDirection : std::uint8_t
{
    /// Smallest first.
    Ascending = 0u,

    /// Largest first.
    Descending = 1u
};

/// Orders a version-one star list by any of the fields `StarProfileV1SortKey` names.
///
/// Equal keys keep their input order. Runs on the calling thread and allocates no engine memory.
///
/// @param stars the list to reorder in place.
/// @param key the persisted field to compare.
/// @param direction which way to order it.
/// @return success; `ErrorCode::InvalidArgument` for an out-of-range key or direction, leaving the
///         list untouched; or the translation of an allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Status
sortStarProfileV1(std::vector<StarProfileV1Star>& stars, StarProfileV1SortKey key,
                  StarProfileSortDirection direction = StarProfileSortDirection::Descending);

/// Parses and validates one version-one or version-two profile document.
///
/// The document is one JSON object holding a `stars_profile` object. An integer `version` of 1 is
/// version one whatever the `engine` member says, an `engine` of `"v1"` with no `version` is version
/// one too, and version two needs both `engine` `"v2"` and `version` 2. The byte bound is checked
/// before the text is parsed, the star bound before the star array is reserved, and each string's
/// bound as it is read. The external JSON value and exceptions stay inside the implementation
/// adapter.
///
/// @param text borrowed UTF-8 JSON document.
/// @param limits input bounds; every one must be positive.
/// @return the typed record; `ErrorCode::InvalidArgument` for a zero limit;
///         `ErrorCode::LimitExceeded` when the document, the star array or a string exceeds its
///         bound, carrying the size and the bound; `ErrorCode::InvalidData` for text that is not
///         JSON, a missing `stars_profile`, `params` or `global` object, a missing version-two
///         field, or a member of the wrong type; `ErrorCode::Unsupported` for any other
///         combination of `engine` and `version`; or a translated allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarProfileFile>
parseStarProfileDocument(std::string_view text, const StarProfileReadLimits& limits = {});

/// Serializes one version-one or version-two profile document.
///
/// The active payload must agree with `profile.generation`. Version two does not persist
/// `StarProfileV2::manuallyEdited`, which its schema has no member for. The external JSON value
/// and exceptions stay inside the implementation adapter.
///
/// @param profile the record to write; borrowed for the call.
/// @param limits output bounds, applied to the star count, each string and the finished text; every
///        one must be positive.
/// @return JSON text; `ErrorCode::IncompatibleArguments` when the generation is unknown or disagrees
///         with the payload; `ErrorCode::InvalidArgument` for a zero limit, a PSF model outside its
///         enumeration or a string that is not UTF-8; `ErrorCode::LimitExceeded` when the star
///         array, a string or the finished text exceeds its bound; or a translated allocation
///         failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::string>
writeStarProfileDocument(const StarProfileFile& profile, const StarProfileReadLimits& limits = {});

} // namespace an
