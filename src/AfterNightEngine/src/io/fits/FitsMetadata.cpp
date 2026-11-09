// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - a FITS image's typed metadata, built from its cards.
//
// The acquisition facts come from the shared card projection and the astrometric solution from the
// engine's own WCS bridge, so the codec keeps no second parser whose crop or distortion rules could
// drift from it. A region's solution, and the WCS cards beside it, are cropped to the region.

#include "io/fits/FitsMetadata.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>

#include <string>
#include <utility>

namespace an
{
Result<ImageMetadata> fitsMetadata(CodecMetadata codec, const ImageExtent& extent, std::string_view path,
                                   FitsInteger64Policy integer64, bool integerImage, const CodecRegion* crop)
{
    const auto projection = projectAcquisition(codec);
    auto acquisition = AcquisitionMetadata::create(projection.acquisition);
    if (!acquisition.hasValue())
        return acquisition.status();
    AstrometricSolution astrometry;
    // An unsupported or incomplete WCS never prevents access to otherwise readable pixels.
    // Its cards remain available for the caller to obtain the bridge's precise typed refusal.
    if (checkWcsCards(codec.cards()) == WcsCardError::None)
    {
        auto solution = parseWcsCards(codec.cards(), extent.width(), extent.height());
        if (!solution.hasValue())
            return solution.status();
        if (crop != nullptr)
        {
            solution = cropWcsSolution(*solution, crop->x, crop->y, crop->width, crop->height);
            if (!solution.hasValue())
                return solution.status();
            auto updated = withWcsCards(codec, *solution);
            if (!updated.hasValue())
                return updated.status();
            codec = std::move(*updated);
        }
        auto summary = solution->toAstrometricSolution();
        if (!summary.hasValue())
            return summary.status();
        astrometry = std::move(*summary);
    }
    auto provenance = Provenance::create(
        ProvenanceSourceKind::File,
        path.size() <= kMaxProvenanceTextBytes ? std::string(path) : "FITS source path exceeds lineage text bound");
    if (!provenance.hasValue())
        return provenance.status();
    if (integerImage && integer64 == FitsInteger64Policy::AllowLossyFloat64)
    {
        auto entry = ProvenanceEntry::create("FITS integer decode",
                                             "Float64 precision loss explicitly acknowledged before scaling");
        if (!entry.hasValue())
            return entry.status();
        provenance = provenance->appended(*entry);
        if (!provenance.hasValue())
            return provenance.status();
    }
    return ImageMetadata::create(std::move(*acquisition), {}, std::move(astrometry), std::move(*provenance),
                                 std::move(codec), std::nullopt);
}
} // namespace an
