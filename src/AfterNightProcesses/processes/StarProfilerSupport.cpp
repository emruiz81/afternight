// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The detector both star-profiler layouts run, the catalog enrichment of the second, the figures their tables show and
/// order by, and the documents they write and read.

#include "stdafx.hpp"

#include "processes/StarProfilerSupport.h"

#include "app/GaiaCatalogSource.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/StarProfileStars.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Photometry/PhotometricCalibration.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace
{

using an::ui::processes::FirstProfilerColumn;
using an::ui::processes::SecondProfilerColumn;
using an::ui::processes::StarEnrichment;
using an::ui::processes::StarEnrichmentTable;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

/// The smallest axis ratio the first layout's search admits: its roundness tolerance holds a star's shape instead.
constexpr double kFirstProfilerMinimumRoundness = 0.01;

/// The first layout's relation of its mean radius to its radius, for the column that shows it.
constexpr double kFirstProfilerMeanRadiusPerRadius = 1.5 / 2.35;

/// The description a star search reads its samples under: the image's, restated in the working representation and in
/// the normalized linear domain every image is measured in.
[[nodiscard]] an::Result<an::ImageDescriptor> searchDescription(const an::ImageDescriptor& described,
                                                                const an::SampleType working)
{
    const an::Result<an::ImageDescriptor> restated = an::describeSamplesAs(described, working);
    if (!restated.hasValue())
    {
        return restated.status();
    }
    an::ImageDescriptorRequest request = restated->toRequest();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    if (const std::optional<an::ImageDescriptor> linear = an::ImageDescriptor::create(request); linear.has_value())
    {
        return *linear;
    }
    // A colour space that has no linear form is not one a search for stars reads.
    request.colorSpace = an::ColorSpace::Unspecified;
    if (const std::optional<an::ImageDescriptor> linear = an::ImageDescriptor::create(request); linear.has_value())
    {
        return *linear;
    }
    return refusal(an::ErrorCode::InvalidArgument, "the image cannot be described for a star search");
}

/// The engine's ordering key for a first-layout column, or nothing for a column ordered by its own figure.
[[nodiscard]] std::optional<an::StarSortKey> engineKeyOf(const FirstProfilerColumn column) noexcept
{
    switch (column)
    {
    case FirstProfilerColumn::X:
        return an::StarSortKey::PositionX;
    case FirstProfilerColumn::Y:
        return an::StarSortKey::PositionY;
    case FirstProfilerColumn::Intensity:
        return an::StarSortKey::Amplitude;
    case FirstProfilerColumn::Quality:
        return an::StarSortKey::Circularity;
    case FirstProfilerColumn::Radius:
    case FirstProfilerColumn::MeanRadius:
        return an::StarSortKey::MeanFullWidth;
    case FirstProfilerColumn::LargeMajorAxis:
    case FirstProfilerColumn::SmallMajorAxis:
        return an::StarSortKey::FullWidthX;
    case FirstProfilerColumn::LargeMinorAxis:
    case FirstProfilerColumn::SmallMinorAxis:
        return an::StarSortKey::FullWidthY;
    case FirstProfilerColumn::MajorAxisAngle:
        return an::StarSortKey::Angle;
    case FirstProfilerColumn::Percentage:
    case FirstProfilerColumn::DeltaRadius:
    default:
        return std::nullopt;
    }
}

/// The engine's ordering key for a second-layout column, or nothing for a column a catalog gives.
[[nodiscard]] std::optional<an::StarSortKey> engineKeyOf(const SecondProfilerColumn column) noexcept
{
    switch (column)
    {
    case SecondProfilerColumn::X:
        return an::StarSortKey::PositionX;
    case SecondProfilerColumn::Y:
        return an::StarSortKey::PositionY;
    case SecondProfilerColumn::Magnitude:
        return an::StarSortKey::InstrumentalMagnitude;
    case SecondProfilerColumn::FullWidthX:
        return an::StarSortKey::FullWidthX;
    case SecondProfilerColumn::FullWidthY:
        return an::StarSortKey::FullWidthY;
    case SecondProfilerColumn::Angle:
        return an::StarSortKey::Angle;
    case SecondProfilerColumn::Background:
        return an::StarSortKey::Background;
    case SecondProfilerColumn::Amplitude:
        return an::StarSortKey::Amplitude;
    case SecondProfilerColumn::Beta:
        return an::StarSortKey::Beta;
    case SecondProfilerColumn::Ratio:
        return an::StarSortKey::Roundness;
    case SecondProfilerColumn::ResidualRootMeanSquare:
        return an::StarSortKey::ResidualRootMeanSquare;
    case SecondProfilerColumn::FullWidth:
        return an::StarSortKey::MeanFullWidth;
    case SecondProfilerColumn::Circularity:
        return an::StarSortKey::Circularity;
    case SecondProfilerColumn::RightAscension:
    case SecondProfilerColumn::Declination:
    default:
        return std::nullopt;
    }
}

/// Orders a list by a figure, stably; a star without the figure comes after the others ascending and before them
/// descending.
template <typename FigureT> void orderByFigure(an::ui::StarList& stars, const bool descending, FigureT figure)
{
    std::stable_sort(stars.begin(), stars.end(),
                     [&figure, descending](const an::DetectedStar& left, const an::DetectedStar& right) {
                         const std::optional<double> a = figure(left);
                         const std::optional<double> b = figure(right);
                         if (!a.has_value() || !b.has_value())
                         {
                             // Absent last ascending, absent first descending: the same rule in both directions.
                             return descending ? (!a.has_value() && b.has_value()) : (a.has_value() && !b.has_value());
                         }
                         return descending ? *a > *b : *a < *b;
                     });
}

/// Orders a list by an engine key, falling back to a figure when the engine refuses.
void orderByEngineKey(an::ui::StarList& stars, const an::StarSortKey key, const bool descending)
{
    const an::Status sorted =
        an::sortStars(stars, key, descending ? an::StarSortDirection::Descending : an::StarSortDirection::Ascending);
    if (sorted.failed())
    {
        LOG_WARNING("Star profiler: the stars could not be ordered by {}: {}", an::starSortKeyName(key),
                    sorted.message());
    }
}

[[nodiscard]] an::PersistedPsfModel persistedModelOf(const an::PsfModel model) noexcept
{
    switch (model)
    {
    case an::PsfModel::MoffatFreeBeta:
        return an::PersistedPsfModel::MoffatFreeBeta;
    case an::PsfModel::MoffatFixedBeta:
        return an::PersistedPsfModel::MoffatFixedBeta;
    case an::PsfModel::Gaussian:
    default:
        return an::PersistedPsfModel::Gaussian;
    }
}

} // namespace

namespace an::ui::processes
{

// ---- What each layout asks for.

an::StarDetectionRequest firstProfilerDetection(const FirstProfilerSettings& settings) noexcept
{
    an::StarDetectionRequest request;
    request.minimumRoundness = kFirstProfilerMinimumRoundness;
    request.maximumRoundness = 1.0;
    // The first layout keeps a star whose peak stands the threshold above the sky; the fitted amplitude is that height.
    request.minimumAmplitude = (std::max)(settings.detectionThreshold, 0.0);
    request.maximumAmplitude = (std::numeric_limits<double>::max)();
    request.model = an::PsfModel::Gaussian;
    return request;
}

bool firstProfilerKeeps(const an::DetectedStar& star, const FirstProfilerSettings& settings) noexcept
{
    const double sigmaX = star.psf.fullWidthX / an::kGaussianFullWidthPerSigma;
    const double sigmaY = star.psf.fullWidthY / an::kGaussianFullWidthPerSigma;
    if (!(std::abs(sigmaX - sigmaY) < settings.roundnessTolerance))
    {
        return false;
    }
    const double size = star.metrics.meanFullWidth;
    return size >= static_cast<double>(settings.minimumSize) && size <= static_cast<double>(settings.maximumSize);
}

an::StarDetectionRequest secondProfilerDetection(const SecondProfilerSettings& settings) noexcept
{
    an::StarDetectionRequest request;
    request.searchRadius = static_cast<std::uint32_t>((std::max)(settings.radius, 1));
    request.noiseSigma = settings.sigma;
    request.minimumRoundness = settings.roundness;
    request.maximumRoundness = settings.roundnessRange ? settings.maximumRoundness : 1.0;
    request.model = settings.moffat ? an::PsfModel::MoffatFreeBeta : an::PsfModel::Gaussian;
    request.minimumBeta = settings.minimumBeta;
    // Both bounds zero is no amplitude window, the rule for an unchecked range.
    request.minimumAmplitude = settings.amplitudeRange ? settings.minimumAmplitude : 0.0;
    request.maximumAmplitude = settings.amplitudeRange ? settings.maximumAmplitude : 0.0;
    request.convergence = static_cast<std::uint32_t>((std::max)(settings.convergence, 1));
    request.relaxedChecks = settings.relaxChecks;
    return request;
}

// ---- A detection.

an::Result<ProfileDetection> detectProfileStars(const an::ui::ExecutionScope& scope,
                                                const an::ui::ProcessSource& source,
                                                const an::StarDetectionRequest& request, const bool useLightness)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::EngineContext& engine = scope.engine();
    const bool colour = process_utils::describesColourImage(*described);
    const bool lightness = useLightness && colour;
    std::optional<an::consumer::ImageAsset> lightnessImage;
    std::optional<an::consumer::AssetReadLease> lightnessLease;
    an::Result<process_utils::NormalizedCopy> searched =
        refusal(an::ErrorCode::NotInitialized, "nothing was read for the search");
    if (lightness)
    {
        an::Result<an::consumer::ImageAsset> extracted = process_utils::extractLightness(scope, source);
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
        lightnessImage.emplace(std::move(*extracted));
        an::Result<an::consumer::AssetReadLease> lease = lightnessImage->read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        lightnessLease.emplace(std::move(*lease));
        searched = process_utils::normalizedCopyOf(scope, an::ui::ProcessSource::of(*lightnessLease));
    }
    else
    {
        searched = process_utils::normalizedCopyOf(scope, source);
    }
    if (!searched.hasValue())
    {
        return searched.status();
    }
    const process_utils::NormalizedSamples& samples = searched->samples;
    const an::Result<an::ImageDescriptor> searchDescribed =
        lightness ? process_utils::derivedImageDescriptor(*described, 1u, samples.sampleType(), engine.limits())
                  : searchDescription(*described, samples.sampleType());
    if (!searchDescribed.hasValue())
    {
        return searchDescribed.status();
    }
    // The frame view borrows its layout, so the layout it is given outlives the search.
    const an::ImageLayout searchLayout = samples.layout();
    const an::KernelValidityView validity = process_utils::skippingNonFinite(samples.validity());
    const an::Result<an::StarFrameView> frame = an::StarFrameView::create(samples.samples(), searchLayout.totalBytes(),
                                                                          searchLayout, *searchDescribed, validity);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::StarDetectionRequest asked = request;
    asked.fullScale = 1.0;
    const std::uint32_t channel = colour && !lightness ? 1u : 0u;
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(asked, *searchDescribed, searchLayout, channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::StarDetectionResult detected;
    if (an::Status found = an::detectStars(scope.execution(), *frame, *plan, detected); found.failed())
    {
        return found;
    }
    ProfileDetection result;
    result.stars = std::move(detected.stars);
    result.summary = detected.summary;
    result.report = detected.report;
    result.channelCount = described->extent().channelCount();
    return result;
}

// ---- What a catalog adds.

an::Result<ProfileEnrichment> enrichProfileStars(const an::ExecutionContext& execution,
                                                 const std::span<const an::DetectedStar> stars,
                                                 const an::WcsSolution& solution,
                                                 const ProfileEnrichmentRequest& request)
{
    ProfileEnrichment enrichment;
    enrichment.stars.resize(stars.size());
    for (std::size_t index = 0u; index < stars.size(); ++index)
    {
        if (an::Status checked = execution.checkCancellation(); checked.failed())
        {
            return checked;
        }
        // The star's position is an engine pixel coordinate, which is what the solution projects: nothing is shifted.
        const an::Result<an::SkyCoordinate> direction =
            solution.pixelToWorld({.x = stars[index].positionX, .y = stars[index].positionY});
        if (direction.hasValue())
        {
            enrichment.stars[index].direction = *direction;
            ++enrichment.placedCount;
        }
    }
    if (request.catalogDirectory.isEmpty() || stars.empty())
    {
        return enrichment;
    }

    const auto failed = [&enrichment](an::Status status) -> an::Result<ProfileEnrichment> {
        if (status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded)
        {
            return status;
        }
        enrichment.catalogFailure = std::move(status);
        return std::move(enrichment);
    };
    const an::Result<an::SkyCoordinate> center = solution.fieldCenter();
    if (!center.hasValue())
    {
        return failed(center.status());
    }
    const an::Result<double> radius = solution.queryRadiusDegrees(request.matchRadiusArcseconds);
    if (!radius.hasValue())
    {
        return failed(radius.status());
    }
    an::Result<an::ui::GaiaCatalogSource> catalog =
        an::ui::GaiaCatalogSource::open(execution, request.catalogDirectory);
    if (!catalog.hasValue())
    {
        return failed(catalog.status());
    }
    an::Result<an::ui::GaiaPhotometricRegion> region =
        catalog->regionWithColors(execution, *center, *radius, request.observationEpochYear, request.magnitudeLimit);
    if (!region.hasValue())
    {
        // A field whose cone holds no catalog star matches nothing; that is an answer, not a failure.
        if (region.status().code() == an::ErrorCode::EmptySelection)
        {
            return enrichment;
        }
        return failed(region.status());
    }

    an::PhotometricCrossMatchRequest matching;
    matching.match.matchRadiusArcseconds = request.matchRadiusArcseconds;
    matching.match.magnitudeLimit = request.magnitudeLimit;
    // The zero point reads each star's box sum back from its instrumental magnitude.
    matching.fluxEstimator = an::StarFluxEstimator::BoxSum;
    an::Result<an::PhotometricCrossMatchResult> matched =
        an::crossMatchDetectedStars(execution, solution, stars, region->catalog, matching);
    if (!matched.hasValue())
    {
        return failed(matched.status());
    }
    for (const an::CatalogMatch& match : matched->matches.matches)
    {
        const an::CatalogEntry* entry = region->catalog.entry(match.entryIndex);
        if (entry == nullptr || match.sourceIndex >= enrichment.stars.size())
        {
            continue;
        }
        StarCatalogMatch found;
        found.identifier = entry->identifier;
        found.magnitudeG = entry->magnitude.value_or(0.0);
        if (match.entryIndex < region->colorIndexBpMinusRp.size())
        {
            found.colorIndexBpMinusRp = region->colorIndexBpMinusRp[match.entryIndex];
        }
        found.separationArcseconds = match.separationArcseconds;
        enrichment.stars[match.sourceIndex].match = std::move(found);
        ++enrichment.figures.matchedCount;
    }
    enrichment.figures.limitingMagnitude = matched->matches.faintestMatchedMagnitude;
    if (!matched->photometry.empty())
    {
        const an::Result<an::ZeroPointFitResult> zeroPoint =
            an::fitPhotometricZeroPoint(execution, matched->photometry);
        if (zeroPoint.hasValue())
        {
            enrichment.figures.zeroPointG = zeroPoint->zeroPoint;
            enrichment.figures.zeroPointError = zeroPoint->zeroPointStandardUncertainty;
        }
        else if (zeroPoint.status().code() == an::ErrorCode::Cancelled ||
                 zeroPoint.status().code() == an::ErrorCode::DeadlineExceeded)
        {
            return zeroPoint.status();
        }
    }
    return enrichment;
}

StarPositionKey starPositionKey(const an::DetectedStar& star) noexcept
{
    return {star.positionX, star.positionY};
}

StarEnrichmentTable enrichmentTable(const std::span<const an::DetectedStar> stars, const ProfileEnrichment& enrichment)
{
    StarEnrichmentTable table;
    const std::size_t count = (std::min)(stars.size(), enrichment.stars.size());
    for (std::size_t index = 0u; index < count; ++index)
    {
        table.insert_or_assign(starPositionKey(stars[index]), enrichment.stars[index]);
    }
    return table;
}

const StarEnrichment* enrichmentOf(const StarEnrichmentTable& table, const an::DetectedStar& star)
{
    const auto found = table.find(starPositionKey(star));
    return found != table.end() ? &found->second : nullptr;
}

// ---- The tables.

double firstProfilerFigure(const an::DetectedStar& star, const FirstProfilerColumn column) noexcept
{
    const double radius = star.metrics.meanFullWidth / 2.0;
    switch (column)
    {
    case FirstProfilerColumn::X:
        return star.positionX;
    case FirstProfilerColumn::Y:
        return star.positionY;
    case FirstProfilerColumn::Intensity:
        return star.psf.amplitude;
    case FirstProfilerColumn::Percentage:
        return 1.0;
    case FirstProfilerColumn::DeltaRadius:
        return std::abs(star.psf.fullWidthX - star.psf.fullWidthY) / 2.0;
    case FirstProfilerColumn::Quality:
        return star.metrics.circularity;
    case FirstProfilerColumn::Radius:
        return radius;
    case FirstProfilerColumn::MeanRadius:
        return radius * kFirstProfilerMeanRadiusPerRadius;
    case FirstProfilerColumn::LargeMajorAxis:
    case FirstProfilerColumn::SmallMajorAxis:
        return star.psf.fullWidthX / 2.0;
    case FirstProfilerColumn::LargeMinorAxis:
    case FirstProfilerColumn::SmallMinorAxis:
        return star.psf.fullWidthY / 2.0;
    case FirstProfilerColumn::MajorAxisAngle:
        return star.psf.angleDegrees;
    default:
        return 0.0;
    }
}

std::optional<double> secondProfilerFigure(const an::DetectedStar& star, const StarEnrichment* enrichment,
                                           const SecondProfilerColumn column) noexcept
{
    switch (column)
    {
    case SecondProfilerColumn::X:
        return star.positionX;
    case SecondProfilerColumn::Y:
        return star.positionY;
    case SecondProfilerColumn::Magnitude:
        return star.instrumentalMagnitude;
    case SecondProfilerColumn::FullWidthX:
        return star.psf.fullWidthX;
    case SecondProfilerColumn::FullWidthY:
        return star.psf.fullWidthY;
    case SecondProfilerColumn::RightAscension:
        if (enrichment != nullptr && enrichment->direction.has_value())
        {
            return enrichment->direction->rightAscensionDegrees;
        }
        return std::nullopt;
    case SecondProfilerColumn::Declination:
        if (enrichment != nullptr && enrichment->direction.has_value())
        {
            return enrichment->direction->declinationDegrees;
        }
        return std::nullopt;
    case SecondProfilerColumn::Angle:
        return star.psf.angleDegrees;
    case SecondProfilerColumn::Background:
        return star.psf.background;
    case SecondProfilerColumn::Amplitude:
        return star.psf.amplitude;
    case SecondProfilerColumn::Beta:
        return star.psf.beta;
    case SecondProfilerColumn::Ratio:
        return star.metrics.roundness;
    case SecondProfilerColumn::ResidualRootMeanSquare:
        return star.psf.residualRootMeanSquare;
    case SecondProfilerColumn::FullWidth:
        return star.metrics.meanFullWidth;
    case SecondProfilerColumn::Circularity:
        return star.metrics.circularity;
    default:
        return std::nullopt;
    }
}

QString figureText(const std::optional<double> figure, const int decimals)
{
    if (!figure.has_value() || !std::isfinite(*figure))
    {
        return QStringLiteral("-");
    }
    return QString::number(*figure, 'f', decimals);
}

an::ui::StarList orderedForFirstProfiler(an::ui::StarList stars, const FirstProfilerColumn column,
                                         const bool descending)
{
    if (const std::optional<an::StarSortKey> key = engineKeyOf(column); key.has_value())
    {
        orderByEngineKey(stars, *key, descending);
        return stars;
    }
    orderByFigure(stars, descending, [column](const an::DetectedStar& star) -> std::optional<double> {
        return firstProfilerFigure(star, column);
    });
    return stars;
}

an::ui::StarList orderedForSecondProfiler(an::ui::StarList stars, const StarEnrichmentTable& enrichment,
                                          const SecondProfilerColumn column, const bool descending)
{
    if (const std::optional<an::StarSortKey> key = engineKeyOf(column); key.has_value())
    {
        orderByEngineKey(stars, *key, descending);
        return stars;
    }
    orderByFigure(stars, descending, [&enrichment, column](const an::DetectedStar& star) {
        return secondProfilerFigure(star, enrichmentOf(enrichment, star), column);
    });
    return stars;
}

// ---- The documents.

an::StarProfileV1 firstProfileHeader(const FirstProfilerSettings& settings, const an::StarFieldSummary& summary)
{
    an::StarProfileV1 header;
    header.parameters.detectionThreshold = settings.detectionThreshold;
    header.parameters.roundnessTolerance = settings.roundnessTolerance;
    header.parameters.minimumSize = static_cast<double>(settings.minimumSize);
    header.parameters.maximumSize = static_cast<double>(settings.maximumSize);
    header.global.fullWidth = summary.medianFullWidth.value_or(0.0);
    header.global.quality = summary.quality.value_or(0.0);
    header.global.background = summary.channel.background;
    header.global.backgroundNoise = summary.channel.backgroundNoise;
    return header;
}

an::StarProfileV2 secondProfileHeader(const SecondProfilerSettings& settings, const an::StarFieldSummary& summary,
                                      const ProfileCatalogFigures& figures, const std::uint32_t channelCount)
{
    an::StarProfileV2 header;
    header.parameters.radius = settings.radius;
    header.parameters.sigma = settings.sigma;
    header.parameters.roundness = settings.roundness;
    header.parameters.convergence = settings.convergence;
    header.parameters.relaxChecks = settings.relaxChecks;
    header.parameters.profile =
        settings.moffat ? an::PersistedPsfModel::MoffatFreeBeta : an::PersistedPsfModel::Gaussian;
    header.parameters.minimumBeta = settings.minimumBeta;
    header.parameters.minimumAmplitude = settings.amplitudeRange ? settings.minimumAmplitude : 0.0;
    header.parameters.maximumAmplitude = settings.amplitudeRange ? settings.maximumAmplitude : 0.0;
    header.parameters.maximumRoundness = settings.roundnessRange ? settings.maximumRoundness : 1.0;
    header.global.fullWidth = summary.medianFullWidth.value_or(0.0);
    header.global.roundness = summary.medianRoundness.value_or(0.0);
    header.global.quality = summary.quality.value_or(0.0);
    header.global.signalToNoise = summary.medianSignalToNoise.value_or(0.0);
    header.global.background = summary.channel.background;
    header.global.backgroundNoise = summary.channel.backgroundNoise;
    header.global.medianAbsoluteDeviation = summary.channel.medianAbsoluteDeviation.value_or(0.0);
    header.global.robustScale = summary.channel.robustScale.value_or(0.0);
    header.global.zeroPointG = figures.zeroPointG.value_or(0.0);
    header.global.zeroPointError = figures.zeroPointError.value_or(0.0);
    header.global.catalogMatchedCount = figures.matchedCount;
    header.global.limitingMagnitude = figures.limitingMagnitude.value_or(0.0);
    header.global.channelCount = (std::max)(channelCount, 1u);
    // The detector measured one channel; its figures stand in the slot of the channel it measured.
    const std::uint32_t slot = (std::min)(summary.channel.channelIndex, 2u);
    header.global.channelBackgroundNoise[slot] = summary.channel.backgroundNoise;
    header.global.channelMedianAbsoluteDeviation[slot] = summary.channel.medianAbsoluteDeviation.value_or(0.0);
    header.global.channelRobustScale[slot] = summary.channel.robustScale.value_or(0.0);
    return header;
}

an::StarProfileV1Star firstProfileStar(const an::DetectedStar& star) noexcept
{
    an::StarProfileV1Star record;
    record.intensity = star.psf.amplitude;
    record.percentage = firstProfilerFigure(star, FirstProfilerColumn::Percentage);
    record.deltaRadius = firstProfilerFigure(star, FirstProfilerColumn::DeltaRadius);
    record.quality = star.metrics.circularity;
    // A profile's readers paint a first-generation star at half its radius's full width.
    record.radius = 2.0 * star.metrics.meanFullWidth;
    record.meanRadius = record.radius * kFirstProfilerMeanRadiusPerRadius;
    record.x = star.positionX;
    record.y = star.positionY;
    record.largeMajorAxis = firstProfilerFigure(star, FirstProfilerColumn::LargeMajorAxis);
    record.smallMajorAxis = firstProfilerFigure(star, FirstProfilerColumn::SmallMajorAxis);
    record.largeMinorAxis = firstProfilerFigure(star, FirstProfilerColumn::LargeMinorAxis);
    record.smallMinorAxis = firstProfilerFigure(star, FirstProfilerColumn::SmallMinorAxis);
    record.majorAxisAngle = star.psf.angleDegrees;
    return record;
}

an::StarProfileV2Star secondProfileStar(const an::DetectedStar& star, const StarEnrichment* enrichment)
{
    an::StarProfileV2Star record;
    record.psf.background = star.psf.background;
    record.psf.amplitude = star.psf.amplitude;
    record.psf.centreX = star.psf.centreX;
    record.psf.centreY = star.psf.centreY;
    record.psf.scaleX = star.psf.scaleX;
    record.psf.scaleY = star.psf.scaleY;
    record.psf.fullWidthX = star.psf.fullWidthX;
    record.psf.fullWidthY = star.psf.fullWidthY;
    record.psf.fullWidthXArcseconds = star.psf.fullWidthXArcseconds.value_or(0.0);
    record.psf.fullWidthYArcseconds = star.psf.fullWidthYArcseconds.value_or(0.0);
    record.psf.angle = star.psf.angleDegrees;
    record.psf.residualRootMeanSquare = star.psf.residualRootMeanSquare;
    record.psf.saturation = star.psf.saturationLevel;
    record.psf.boxRadius = static_cast<std::int32_t>(star.psf.boxRadius);
    record.psf.saturated = star.psf.saturated;
    // The document states minus one for a Gaussian's exponent.
    record.psf.beta = star.psf.beta.value_or(-1.0);
    record.psf.profile = persistedModelOf(star.psf.model);
    record.position.x = star.positionX;
    record.position.y = star.positionY;
    record.photometry.valid = star.instrumentalMagnitude.has_value();
    record.photometry.magnitude = star.instrumentalMagnitude.value_or(0.0);
    record.photometry.signalToNoise = star.signalToNoise.value_or(0.0);
    record.uncertainty.background = star.uncertainty.background.value_or(0.0);
    record.uncertainty.amplitude = star.uncertainty.amplitude.value_or(0.0);
    record.uncertainty.centreX = star.uncertainty.centreX.value_or(0.0);
    record.uncertainty.centreY = star.uncertainty.centreY.value_or(0.0);
    record.uncertainty.scaleX = star.uncertainty.scaleX.value_or(0.0);
    record.uncertainty.scaleY = star.uncertainty.scaleY.value_or(0.0);
    record.uncertainty.angle = star.uncertainty.angle.value_or(0.0);
    record.uncertainty.beta = star.uncertainty.beta.value_or(0.0);
    record.astrometry.channel = static_cast<std::int32_t>(star.channelIndex);
    record.astrometry.units = "px";
    if (enrichment != nullptr && enrichment->direction.has_value())
    {
        record.astrometry.rightAscension = enrichment->direction->rightAscensionDegrees;
        record.astrometry.declination = enrichment->direction->declinationDegrees;
        record.astrometry.hasWorldCoordinates = true;
    }
    if (enrichment != nullptr && enrichment->match.has_value())
    {
        record.astrometry.catalogId = enrichment->match->identifier;
        record.astrometry.catalogMagnitudeG = enrichment->match->magnitudeG;
        record.astrometry.colorIndexBpRp = enrichment->match->colorIndexBpMinusRp.value_or(0.0);
        record.astrometry.matchSeparationArcseconds = enrichment->match->separationArcseconds;
        record.astrometry.hasCatalogMatch = true;
    }
    record.metrics.roundness = star.metrics.roundness;
    record.metrics.circularity = star.metrics.circularity;
    record.metrics.fullWidth = star.metrics.meanFullWidth;
    return record;
}

an::StarProfileFile profileDocument(an::StarProfilePayload header, const std::span<const an::DetectedStar> stars,
                                    const StarEnrichmentTable& enrichment, const bool manuallyEdited)
{
    an::StarProfileFile document;
    if (auto* first = std::get_if<an::StarProfileV1>(&header); first != nullptr)
    {
        first->stars.clear();
        first->stars.reserve(stars.size());
        for (const an::DetectedStar& star : stars)
        {
            first->stars.push_back(firstProfileStar(star));
        }
        first->manuallyEdited = manuallyEdited;
        document.generation = an::StarProfileGeneration::Version1;
    }
    else if (auto* second = std::get_if<an::StarProfileV2>(&header); second != nullptr)
    {
        second->stars.clear();
        second->stars.reserve(stars.size());
        for (const an::DetectedStar& star : stars)
        {
            second->stars.push_back(secondProfileStar(star, enrichmentOf(enrichment, star)));
        }
        second->manuallyEdited = manuallyEdited;
        second->global.catalogMatchedCount = static_cast<std::uint64_t>(
            std::count_if(second->stars.begin(), second->stars.end(),
                          [](const an::StarProfileV2Star& record) { return record.astrometry.hasCatalogMatch; }));
        document.generation = an::StarProfileGeneration::Version2;
    }
    document.payload = std::move(header);
    return document;
}

LoadedProfile loadedProfile(const an::StarProfileFile& document)
{
    LoadedProfile loaded;
    // The positions are read as the document states them, in engine pixel coordinates.
    loaded.stars = process_utils::starsOfProfile(document);
    loaded.header = document.payload;
    if (const auto* second = std::get_if<an::StarProfileV2>(&document.payload); second != nullptr)
    {
        const std::size_t count = (std::min)(second->stars.size(), loaded.stars.size());
        for (std::size_t index = 0u; index < count; ++index)
        {
            const an::StarProfileV2Star& record = second->stars[index];
            StarEnrichment enrichment;
            if (record.astrometry.hasWorldCoordinates)
            {
                enrichment.direction = an::SkyCoordinate{.rightAscensionDegrees = record.astrometry.rightAscension,
                                                         .declinationDegrees = record.astrometry.declination};
            }
            if (record.astrometry.hasCatalogMatch)
            {
                StarCatalogMatch match;
                match.identifier = record.astrometry.catalogId;
                match.magnitudeG = record.astrometry.catalogMagnitudeG;
                match.colorIndexBpMinusRp = record.astrometry.colorIndexBpRp;
                match.separationArcseconds = record.astrometry.matchSeparationArcseconds;
                enrichment.match = std::move(match);
            }
            if (enrichment.direction.has_value() || enrichment.match.has_value())
            {
                loaded.enrichment.insert_or_assign(starPositionKey(loaded.stars[index]), std::move(enrichment));
            }
        }
    }
    std::visit([](auto& payload) { payload.stars.clear(); }, loaded.header);
    return loaded;
}

} // namespace an::ui::processes
