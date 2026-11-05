// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - an XISF image's typed metadata: its acquisition facts from the FITS keywords it carries, and its
// astrometric solution from those keywords or from its AstrometricSolution properties.
//
// XISF carries FITS keywords natively, so the acquisition facts and a solution the keywords state are read through
// the same tables and the same bridge a FITS header goes through. An XISF property is never consulted for an
// acquisition fact. A solution is the one exception, because PixInsight states one in the AstrometricSolution
// namespace alone and no keywords beside it: when the keywords describe no solution, the namespace's first layer is
// translated into the world-coordinate cards a plate solve adds, so the image is solved for everything that reads a
// solution from an image's cards, and the keywords keep their precedence wherever they state one, so which of two
// vocabularies answers never depends on which was written first.

#include "io/xisf/XisfFile.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>

#include <openxisf/astrometry.h>

#include <string>
#include <utility>

namespace an
{
namespace
{
/// The world-coordinate cards an image's AstrometricSolution states, and whether it states more than they hold.
struct TranslatedSolution
{
    std::vector<CodecCard> cards;
    bool beyondLinear = false;
};

/// The cards of an image's AstrometricSolution, when the engine's model holds its first layer.
///
/// The layer is a projection with a linear transformation of image coordinates, which XISF counts like this engine's
/// pixel coordinates - from the image's first column and row, with the centre of the first pixel at one half - so its
/// reference image coordinates are the reference pixel and its matrix, from image coordinates to the projection plane
/// in degrees, the linear matrix. The engine's model holds a gnomonic projection of ICRS directions with the native
/// reference point at the native pole and the celestial pole at native longitude 180, as PixInsight evaluates every
/// gnomonic solution. A solution whose reference point is the north celestial pole and which states no pole has the
/// pole at longitude 0, which is the same solution turned half way round, so its matrix is negated. Anything else
/// gives no cards, and the solution stays in the properties alone.
std::optional<TranslatedSolution> translatedSolution(const openxisf::property_list& properties,
                                                     const ImageExtent& extent)
{
    std::optional<openxisf::astrometric_solution> solution;
    try
    {
        solution.emplace(properties);
    }
    catch (const openxisf::error&)
    {
        return std::nullopt;
    }
    const auto& projection = solution->projection();
    if (!projection || projection->projection_system != openxisf::projection_system::gnomonic ||
        projection->celestial_reference_system != "ICRS" || projection->reference_native[0] != 0.0 ||
        projection->reference_native[1] != 90.0)
        return std::nullopt;
    const auto& matrix = projection->linear_transformation;
    double sign = 1.0;
    if (projection->celestial_pole_native[0] != 180.0)
    {
        if (projection->celestial_pole_native[0] != 0.0 || projection->reference_celestial.dec != 90.0)
            return std::nullopt;
        sign = -1.0;
    }
    auto wcs =
        WcsSolution::create({.referencePixel = {.x = projection->reference_image.x, .y = projection->reference_image.y},
                             .referencePoint = {.rightAscensionDegrees = projection->reference_celestial.ra,
                                                .declinationDegrees = projection->reference_celestial.dec},
                             .linear = {.cd11 = sign * matrix[0],
                                        .cd12 = sign * matrix[1],
                                        .cd21 = sign * matrix[2],
                                        .cd22 = sign * matrix[3]},
                             .projection = WcsProjection::Gnomonic,
                             .imageWidth = extent.width(),
                             .imageHeight = extent.height()});
    if (!wcs.hasValue())
        return std::nullopt;
    auto built = buildWcsCards(*wcs);
    if (!built.hasValue())
        return std::nullopt;
    TranslatedSolution translated;
    translated.cards.reserve(built->size());
    for (const CodecCard& card : *built)
    {
        auto marked =
            CodecCard::create(card.origin(), card.keyword(), card.value(), std::string(kXisfTranslatedSolutionComment));
        if (!marked.hasValue())
            return std::nullopt;
        translated.cards.push_back(std::move(*marked));
    }
    translated.beyondLinear = solution->layer() != openxisf::astrometric_layer::linear;
    return translated;
}
} // namespace

Result<ImageMetadata> xisfMetadata(CodecMetadata codec, const ImageExtent& extent, std::string_view path,
                                   const openxisf::property_list& properties)
{
    const auto projection = projectAcquisition(codec);
    auto acquisition = AcquisitionMetadata::create(projection.acquisition);
    if (!acquisition.hasValue())
        return acquisition.status();
    // The translated cards join the file's own only when the two together describe one solution, so a keyword the
    // file states beside them - an equinox, a frame - can never make them describe another.
    bool beyondLinear = false;
    if (checkWcsCards(codec.cards()) != WcsCardError::None)
        if (auto translated = translatedSolution(properties, extent))
        {
            std::vector<CodecCard> cards(codec.cards().begin(), codec.cards().end());
            cards.insert(cards.end(), translated->cards.begin(), translated->cards.end());
            if (cards.size() <= kMaxCodecCards && checkWcsCards(cards) == WcsCardError::None)
            {
                auto stated = CodecMetadata::create(codec.sourceEncoding(), std::move(cards));
                if (!stated.hasValue())
                    return stated.status();
                codec = std::move(*stated);
                beyondLinear = translated->beyondLinear;
            }
        }
    AstrometricSolution astrometry;
    // An unsupported or incomplete solution never prevents access to otherwise readable pixels.
    // Its cards remain available for the caller to obtain the bridge's precise typed refusal.
    if (checkWcsCards(codec.cards()) == WcsCardError::None)
    {
        auto solution = parseWcsCards(codec.cards(), extent.width(), extent.height());
        if (!solution.hasValue())
            return solution.status();
        auto summary = solution->toAstrometricSolution();
        if (!summary.hasValue())
            return summary.status();
        if (auto request = summary->toRequest(); beyondLinear && request)
        {
            request->hasDistortion = true;
            summary = AstrometricSolution::create(*request);
            if (!summary.hasValue())
                return summary.status();
        }
        astrometry = std::move(*summary);
    }
    auto provenance = Provenance::create(
        ProvenanceSourceKind::File,
        path.size() <= kMaxProvenanceTextBytes ? std::string(path) : "XISF source path exceeds lineage text bound");
    if (!provenance.hasValue())
        return provenance.status();
    return ImageMetadata::create(std::move(*acquisition), {}, std::move(astrometry), std::move(*provenance),
                                 std::move(codec), std::nullopt);
}

Result<ImageMetadata> croppedXisfMetadata(const ImageMetadata& metadata, const ImageExtent& extent,
                                          const CodecRegion& crop)
{
    // Only a solution the cards describe has anything to crop; the cards then stand for it, whether the file stated
    // them or they were translated from its AstrometricSolution.
    if (checkWcsCards(metadata.codec().cards()) != WcsCardError::None)
        return metadata;
    auto solution = parseWcsCards(metadata.codec().cards(), extent.width(), extent.height());
    if (!solution.hasValue())
        return solution.status();
    auto cropped = cropWcsSolution(*solution, crop.x, crop.y, crop.width, crop.height);
    if (!cropped.hasValue())
        return cropped.status();
    // The AstrometricSolution properties describe the whole image, so they leave with the cards they gave; what a
    // later write states is the region's solution, once.
    auto codec = withWcsCards(metadata.codec(), *cropped);
    if (!codec.hasValue())
        return codec.status();
    auto summary = cropped->toAstrometricSolution();
    if (!summary.hasValue())
        return summary.status();
    if (auto request = summary->toRequest(); request && metadata.astrometry().hasDistortion().value_or(false))
    {
        request->hasDistortion = true;
        summary = AstrometricSolution::create(*request);
        if (!summary.hasValue())
            return summary.status();
    }
    return ImageMetadata::create(metadata.acquisition(), metadata.semantics(), std::move(*summary),
                                 metadata.provenance(), std::move(*codec), std::nullopt);
}
} // namespace an
