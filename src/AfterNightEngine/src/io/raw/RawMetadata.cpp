// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the source facts a camera RAW file carries, as engine metadata.
//
// The camera's calibration is copied as the file states it - black levels, white level, both
// matrices, as-shot and daylight gains, orientation - so that nothing a later stage computes is
// mistaken for a source fact. A probe reads what LibRaw knows from the header; a decode reads again
// after unpacking, before black subtraction or colour processing can change those values, and records
// in provenance what the decode did.
//
// A decode also reads the Exif the file states, as cards of the camera RAW namespace, through the
// metadata library the raster containers read theirs with. A probe does not: a folder of camera files
// is probed for their typed facts without each file being read again. The Exif is a second library's
// reading of a container LibRaw already read, so Exif it cannot state is a warning, never a refusal of
// the image.

#include "io/raw/RawFile.h"

#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "interop/exiv2/ExifCards.h"
#include "io/metadata/MetadataFile.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace an
{
namespace
{
/// Says on the execution's diagnostic sink that the file's Exif was left out, and why.
void reportExifLeftOut(const ExecutionContext& execution, const Status& status)
{
    const std::string message =
        std::string("the camera file's Exif could not be read, and the image states none of it: ") + status.message();
    execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Io, "read camera RAW Exif", message.c_str());
}
} // namespace

std::vector<CodecCard> rawExifCards(const EngineContext& engine, const ExecutionContext& execution,
                                    std::string_view path)
{
    auto file = MetadataFile::open(engine, path, CodecFormat::Raw, "camera RAW");
    if (!file.hasValue())
    {
        reportExifLeftOut(execution, file.status());
        return {};
    }
    auto cards = readCameraRawExifCards(file->bytes());
    if (!cards.hasValue())
    {
        reportExifLeftOut(execution, cards.status());
        return {};
    }
    return std::move(*cards);
}

Result<ImageMetadata> rawMetadata(const LibRaw& decoder, bool requested, bool decoded, std::vector<CodecCard> exif)
{
    const auto& data = decoder.imgdata;
    AcquisitionMetadataRequest request;
    if (requested)
    {
        request.instrumentName = std::string(data.idata.make) + " " + data.idata.model;
        request.observerName = data.other.artist;
        request.objectName = data.other.desc;
        if (std::isfinite(data.other.shutter) && data.other.shutter > 0.0f)
            request.exposureSeconds = data.other.shutter;
        if (data.other.timestamp > 0 &&
            static_cast<std::uint64_t>(data.other.timestamp) <=
                static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)() / 1000000000))
            request.exposureStart = UtcInstant{static_cast<std::int64_t>(data.other.timestamp) * 1000000000};
        RawCameraMetadata camera;
        camera.blackLevel = data.color.black;
        camera.whiteLevel = data.color.maximum;
        camera.blackPatternRows = data.color.cblack[4];
        camera.blackPatternColumns = data.color.cblack[5];
        const auto count = static_cast<std::uint64_t>(camera.blackPatternRows) * camera.blackPatternColumns;
        if (count > 4096u)
            return Status::failure(ErrorCode::LimitExceeded);
        camera.blackPattern.assign(data.color.cblack + 6, data.color.cblack + 6 + count);
        for (unsigned c = 0u; c < 4u; ++c)
        {
            camera.channelBlack[c] = data.color.cblack[c];
            camera.cameraMultipliers[c] = data.color.cam_mul[c];
            camera.daylightMultipliers[c] = data.color.pre_mul[c];
            for (unsigned i = 0u; i < 3u; ++i)
            {
                camera.cameraToRgb[i][c] = data.color.rgb_cam[i][c];
                camera.cameraToXyz[c][i] = data.color.cam_xyz[c][i];
            }
        }
        camera.isoSpeed = data.other.iso_speed;
        camera.focalLengthMillimeters = data.other.focal_len;
        camera.aperture = data.other.aperture;
        camera.orientation = data.sizes.flip;
        camera.exifColorSpace = data.color.ExifColorSpace;
        camera.asShotWhiteBalanceApplied = data.color.as_shot_wb_applied != 0;
        request.rawCamera = std::move(camera);
    }
    auto acquisition = AcquisitionMetadata::create(request);
    if (!acquisition.hasValue())
        return acquisition.status();
    auto semantics = ImageSemantics::create({.transferFunction = TransferFunction::Linear});
    if (!semantics.hasValue())
        return semantics.status();
    Provenance provenance;
    if (decoded)
    {
        auto entry = ProvenanceEntry::create(
            "decode camera RAW",
            rawFullColor(decoder)
                ? "LibRaw full-colour processing; linear sRGB; explicit planar RGB; orientation retained"
                : "LibRaw black subtraction; camera codes; no white balance, normalization, demosaic or orientation");
        if (!entry.hasValue())
            return entry.status();
        auto created = Provenance::create(ProvenanceSourceKind::File, {});
        if (!created.hasValue())
            return created.status();
        auto appended = created->appended(*entry);
        if (!appended.hasValue())
            return appended.status();
        provenance = std::move(*appended);
    }
    // The cards claim nothing about how the samples were stored, which the frame's own description states.
    auto codec = CodecMetadata::create(SourceEncoding{}, std::move(exif));
    if (!codec.hasValue())
        return codec.status();
    return ImageMetadata::create(std::move(*acquisition), *semantics, {}, std::move(provenance), std::move(*codec),
                                 std::nullopt);
}
} // namespace an
