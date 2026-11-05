// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the rule that decides which preserved cards a destination format writes back.
//
// A card is judged in one fixed order, and the first rule that applies names its disposition. A
// destination that writes no cards (Raw, Dump, Unknown) refuses every card. FITS and XISF write cards
// of their own vocabulary and of the FITS one; JPEG, TIFF and PNG write cards of any raster vocabulary
// or of camera raw; anything else is foreign. A generated file's identity keyword never travels to a
// product derived from it. A FITS structural keyword describes the file it was read from rather than
// the one being written, so it stays behind. A raster destination then writes only the EXIF keys on
// its own allowlist.
//
// The allowlists are held sorted and checked sorted at compile time, so membership is a binary search
// over an immutable table.
//
// An Exif field is described by the metadata library's interop layer and published here as the
// engine's `ExifField`.

#include <AfterNightEngine/IO/CodecMetadataPolicy.h>

#include <AfterNightEngine/IO/ArtifactIdentity.h>

#include "interop/exiv2/ExifCards.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>

namespace an
{
namespace
{
using std::string_view_literals::operator""sv;

/// Structural keywords compared exactly.
constexpr std::array kStructuralKeywords = {
    "SIMPLE"sv,   "BITPIX"sv,   "NAXIS"sv,    "EXTEND"sv,  "BZERO"sv,   "BSCALE"sv,  "BLANK"sv,
    "PCOUNT"sv,   "GCOUNT"sv,   "XTENSION"sv, "END"sv,     "ZIMAGE"sv,  "ZBITPIX"sv, "ZNAXIS"sv,
    "ZCMPTYPE"sv, "ZQUANTIZ"sv, "ZDITHER0"sv, "ZSIMPLE"sv, "ZEXTEND"sv, "ZPCOUNT"sv, "ZGCOUNT"sv,
    "ZTENSION"sv, "TFIELDS"sv,  "THEAP"sv,    "DATASUM"sv, "CHECKSUM"sv};

/// Structural keywords that carry a positive index: NAXIS1, ZTILE2, TFORM3.
constexpr std::array kIndexedStructuralKeywords = {"NAXIS"sv, "ZNAXIS"sv, "ZTILE"sv, "ZNAME"sv, "ZVAL"sv,
                                                   "TTYPE"sv, "TFORM"sv,  "TUNIT"sv, "TDIM"sv,  "TSCAL"sv,
                                                   "TZERO"sv, "TNULL"sv,  "TDISP"sv};

// The three allowlists, in sorted order so membership is a binary search. The PNG table holds sixteen
// keys.
constexpr std::array kJpegExifKeys = {"Exif.Image.BitsPerSample"sv,
                                      "Exif.Image.DateTime"sv,
                                      "Exif.Image.ExifTag"sv,
                                      "Exif.Image.ImageLength"sv,
                                      "Exif.Image.ImageWidth"sv,
                                      "Exif.Image.Make"sv,
                                      "Exif.Image.Model"sv,
                                      "Exif.Image.Orientation"sv,
                                      "Exif.Image.PhotometricInterpretation"sv,
                                      "Exif.Image.ResolutionUnit"sv,
                                      "Exif.Image.SamplesPerPixel"sv,
                                      "Exif.Image.Software"sv,
                                      "Exif.Image.XResolution"sv,
                                      "Exif.Image.YCbCrPositioning"sv,
                                      "Exif.Image.YResolution"sv,
                                      "Exif.Iop.InteroperabilityIndex"sv,
                                      "Exif.Iop.InteroperabilityVersion"sv,
                                      "Exif.Photo.ApertureValue"sv,
                                      "Exif.Photo.CFAPattern"sv,
                                      "Exif.Photo.ColorSpace"sv,
                                      "Exif.Photo.ComponentsConfiguration"sv,
                                      "Exif.Photo.CompressedBitsPerPixel"sv,
                                      "Exif.Photo.Contrast"sv,
                                      "Exif.Photo.CustomRendered"sv,
                                      "Exif.Photo.DateTimeDigitized"sv,
                                      "Exif.Photo.DateTimeOriginal"sv,
                                      "Exif.Photo.DigitalZoomRatio"sv,
                                      "Exif.Photo.ExifVersion"sv,
                                      "Exif.Photo.ExposureBiasValue"sv,
                                      "Exif.Photo.ExposureMode"sv,
                                      "Exif.Photo.ExposureProgram"sv,
                                      "Exif.Photo.ExposureTime"sv,
                                      "Exif.Photo.FNumber"sv,
                                      "Exif.Photo.FileSource"sv,
                                      "Exif.Photo.Flash"sv,
                                      "Exif.Photo.FlashpixVersion"sv,
                                      "Exif.Photo.FocalLength"sv,
                                      "Exif.Photo.FocalLengthIn35mmFilm"sv,
                                      "Exif.Photo.GainControl"sv,
                                      "Exif.Photo.ISOSpeedRatings"sv,
                                      "Exif.Photo.ImageUniqueID"sv,
                                      "Exif.Photo.InteroperabilityTag"sv,
                                      "Exif.Photo.LightSource"sv,
                                      "Exif.Photo.MaxApertureValue"sv,
                                      "Exif.Photo.MeteringMode"sv,
                                      "Exif.Photo.PixelXDimension"sv,
                                      "Exif.Photo.PixelYDimension"sv,
                                      "Exif.Photo.Saturation"sv,
                                      "Exif.Photo.SceneCaptureType"sv,
                                      "Exif.Photo.SceneType"sv,
                                      "Exif.Photo.SensingMethod"sv,
                                      "Exif.Photo.Sharpness"sv,
                                      "Exif.Photo.ShutterSpeedValue"sv,
                                      "Exif.Photo.SubSecTime"sv,
                                      "Exif.Photo.SubSecTimeDigitized"sv,
                                      "Exif.Photo.SubSecTimeOriginal"sv,
                                      "Exif.Photo.SubjectDistanceRange"sv,
                                      "Exif.Photo.UserComment"sv,
                                      "Exif.Photo.WhiteBalance"sv,
                                      "Exif.Thumbnail.Compression"sv,
                                      "Exif.Thumbnail.JPEGInterchangeFormat"sv,
                                      "Exif.Thumbnail.JPEGInterchangeFormatLength"sv,
                                      "Exif.Thumbnail.ResolutionUnit"sv,
                                      "Exif.Thumbnail.XResolution"sv,
                                      "Exif.Thumbnail.YResolution"sv};

constexpr std::array kTiffExifKeys = {"Exif.Image.BitsPerSample"sv,
                                      "Exif.Image.Compression"sv,
                                      "Exif.Image.DateTime"sv,
                                      "Exif.Image.ExifTag"sv,
                                      "Exif.Image.IPTCNAA"sv,
                                      "Exif.Image.ImageLength"sv,
                                      "Exif.Image.ImageResources"sv,
                                      "Exif.Image.ImageWidth"sv,
                                      "Exif.Image.InterColorProfile"sv,
                                      "Exif.Image.Make"sv,
                                      "Exif.Image.Model"sv,
                                      "Exif.Image.NewSubfileType"sv,
                                      "Exif.Image.Orientation"sv,
                                      "Exif.Image.PhotometricInterpretation"sv,
                                      "Exif.Image.PlanarConfiguration"sv,
                                      "Exif.Image.ResolutionUnit"sv,
                                      "Exif.Image.RowsPerStrip"sv,
                                      "Exif.Image.SamplesPerPixel"sv,
                                      "Exif.Image.Software"sv,
                                      "Exif.Image.StripByteCounts"sv,
                                      "Exif.Image.StripOffsets"sv,
                                      "Exif.Image.XMLPacket"sv,
                                      "Exif.Image.XResolution"sv,
                                      "Exif.Image.YResolution"sv,
                                      "Exif.Iop.InteroperabilityIndex"sv,
                                      "Exif.Iop.InteroperabilityVersion"sv,
                                      "Exif.Photo.ApertureValue"sv,
                                      "Exif.Photo.CFAPattern"sv,
                                      "Exif.Photo.ColorSpace"sv,
                                      "Exif.Photo.ComponentsConfiguration"sv,
                                      "Exif.Photo.CompressedBitsPerPixel"sv,
                                      "Exif.Photo.Contrast"sv,
                                      "Exif.Photo.CustomRendered"sv,
                                      "Exif.Photo.DateTimeDigitized"sv,
                                      "Exif.Photo.DateTimeOriginal"sv,
                                      "Exif.Photo.DigitalZoomRatio"sv,
                                      "Exif.Photo.ExifVersion"sv,
                                      "Exif.Photo.ExposureBiasValue"sv,
                                      "Exif.Photo.ExposureMode"sv,
                                      "Exif.Photo.ExposureProgram"sv,
                                      "Exif.Photo.ExposureTime"sv,
                                      "Exif.Photo.FNumber"sv,
                                      "Exif.Photo.FileSource"sv,
                                      "Exif.Photo.Flash"sv,
                                      "Exif.Photo.FlashpixVersion"sv,
                                      "Exif.Photo.FocalLength"sv,
                                      "Exif.Photo.FocalLengthIn35mmFilm"sv,
                                      "Exif.Photo.GainControl"sv,
                                      "Exif.Photo.ISOSpeedRatings"sv,
                                      "Exif.Photo.ImageUniqueID"sv,
                                      "Exif.Photo.InteroperabilityTag"sv,
                                      "Exif.Photo.LightSource"sv,
                                      "Exif.Photo.MaxApertureValue"sv,
                                      "Exif.Photo.MeteringMode"sv,
                                      "Exif.Photo.PixelXDimension"sv,
                                      "Exif.Photo.PixelYDimension"sv,
                                      "Exif.Photo.Saturation"sv,
                                      "Exif.Photo.SceneCaptureType"sv,
                                      "Exif.Photo.SceneType"sv,
                                      "Exif.Photo.SensingMethod"sv,
                                      "Exif.Photo.Sharpness"sv,
                                      "Exif.Photo.ShutterSpeedValue"sv,
                                      "Exif.Photo.SubSecTime"sv,
                                      "Exif.Photo.SubSecTimeDigitized"sv,
                                      "Exif.Photo.SubSecTimeOriginal"sv,
                                      "Exif.Photo.SubjectDistanceRange"sv,
                                      "Exif.Photo.UserComment"sv,
                                      "Exif.Photo.WhiteBalance"sv};

constexpr std::array kPngExifKeys = {"Exif.Image.DateTime"sv,
                                     "Exif.Image.ImageLength"sv,
                                     "Exif.Image.ImageWidth"sv,
                                     "Exif.Image.Make"sv,
                                     "Exif.Image.Model"sv,
                                     "Exif.Image.Software"sv,
                                     "Exif.Photo.ApertureValue"sv,
                                     "Exif.Photo.DateTimeDigitized"sv,
                                     "Exif.Photo.DateTimeOriginal"sv,
                                     "Exif.Photo.ExifVersion"sv,
                                     "Exif.Photo.ExposureBiasValue"sv,
                                     "Exif.Photo.ExposureMode"sv,
                                     "Exif.Photo.ExposureProgram"sv,
                                     "Exif.Photo.ExposureTime"sv,
                                     "Exif.Photo.FNumber"sv,
                                     "Exif.Photo.ISOSpeedRatings"sv};

static_assert(std::is_sorted(kJpegExifKeys.begin(), kJpegExifKeys.end()));
static_assert(std::is_sorted(kTiffExifKeys.begin(), kTiffExifKeys.end()));
static_assert(std::is_sorted(kPngExifKeys.begin(), kPngExifKeys.end()));

bool positiveIndex(std::string_view digits) noexcept
{
    if (digits.empty() || digits.size() > 3u || digits.front() == '0')
        return false;
    for (const char value : digits)
        if (value < '0' || value > '9')
            return false;
    return true;
}

bool rasterNamespace(CodecNamespace origin) noexcept
{
    return origin == CodecNamespace::Jpeg || origin == CodecNamespace::Tiff || origin == CodecNamespace::Png ||
           origin == CodecNamespace::CameraRaw;
}

bool rasterDestination(CodecFormat destination) noexcept
{
    return destination == CodecFormat::Jpeg || destination == CodecFormat::Tiff || destination == CodecFormat::Png;
}
} // namespace

const char* cardDispositionName(CardDisposition disposition) noexcept
{
    switch (disposition)
    {
    case CardDisposition::Written:
        return "Written";
    case CardDisposition::ForeignNamespace:
        return "ForeignNamespace";
    case CardDisposition::StructuralKeyword:
        return "StructuralKeyword";
    case CardDisposition::IdentityKeyword:
        return "IdentityKeyword";
    case CardDisposition::NotInAllowlist:
        return "NotInAllowlist";
    case CardDisposition::UnsupportedDestination:
        return "UnsupportedDestination";
    }
    return "UnknownCardDisposition";
}

bool isStructuralFitsKeyword(std::string_view keyword) noexcept
{
    for (const auto structural : kStructuralKeywords)
        if (keyword == structural)
            return true;
    for (const auto base : kIndexedStructuralKeywords)
        if (keyword.size() > base.size() && keyword.starts_with(base) && positiveIndex(keyword.substr(base.size())))
            return true;
    return false;
}

std::span<const std::string_view> writableExifKeys(CodecFormat destination) noexcept
{
    switch (destination)
    {
    case CodecFormat::Jpeg:
        return kJpegExifKeys;
    case CodecFormat::Tiff:
        return kTiffExifKeys;
    case CodecFormat::Png:
        return kPngExifKeys;
    case CodecFormat::Unknown:
    case CodecFormat::Fits:
    case CodecFormat::Xisf:
    case CodecFormat::Raw:
    case CodecFormat::Dump:
        break;
    }
    return {};
}

CardDisposition cardDisposition(CodecFormat destination, const CodecCard& card) noexcept
{
    if (!isCodecFormat(destination) || destination == CodecFormat::Unknown || destination == CodecFormat::Raw ||
        destination == CodecFormat::Dump)
        return CardDisposition::UnsupportedDestination;
    const CodecNamespace origin = card.origin();
    if (!isCodecNamespace(origin))
        return CardDisposition::ForeignNamespace;
    if (destination == CodecFormat::Fits || destination == CodecFormat::Xisf)
    {
        const bool spoken = origin == codecNamespaceOf(destination) || origin == CodecNamespace::Fits;
        if (!spoken)
            return CardDisposition::ForeignNamespace;
        if (isArtifactIdentityKeyword(card.keyword()))
            return CardDisposition::IdentityKeyword;
        if (origin == CodecNamespace::Fits && isStructuralFitsKeyword(card.keyword()))
            return CardDisposition::StructuralKeyword;
        return CardDisposition::Written;
    }
    if (!rasterDestination(destination) || !rasterNamespace(origin))
        return CardDisposition::ForeignNamespace;
    if (isArtifactIdentityKeyword(card.keyword()))
        return CardDisposition::IdentityKeyword;
    const std::span<const std::string_view> keys = writableExifKeys(destination);
    return std::binary_search(keys.begin(), keys.end(), std::string_view(card.keyword()))
               ? CardDisposition::Written
               : CardDisposition::NotInAllowlist;
}

WritableCardReport writableCards(const CodecMetadata& source, CodecFormat destination)
{
    WritableCardReport report;
    for (const CodecCard& card : source.cards())
    {
        switch (cardDisposition(destination, card))
        {
        case CardDisposition::Written:
            report.cards.push_back(card);
            break;
        case CardDisposition::ForeignNamespace:
            ++report.droppedForeign;
            break;
        case CardDisposition::StructuralKeyword:
            ++report.droppedStructural;
            break;
        case CardDisposition::IdentityKeyword:
            ++report.droppedIdentity;
            break;
        case CardDisposition::NotInAllowlist:
            ++report.droppedNotAllowed;
            break;
        case CardDisposition::UnsupportedDestination:
            ++report.droppedUnsupported;
            break;
        }
    }
    return report;
}

Result<ImageMetadata> outputMetadata(const ImageMetadata& source, CodecFormat destination)
{
    WritableCardReport writable = writableCards(source.codec(), destination);
    Result<CodecMetadata> codec = CodecMetadata::create(source.codec().sourceEncoding(), std::move(writable.cards));
    if (!codec.hasValue())
        return codec.status();
    return ImageMetadata::create(source.acquisition(), source.semantics(), source.astrometry(), source.provenance(),
                                 std::move(*codec), std::nullopt);
}

Result<std::vector<ExifField>> describeExifFields(std::span<const CodecCard> cards)
{
    auto described = describeExifCards(cards);
    if (!described.hasValue())
    {
        return described.status();
    }
    std::vector<ExifField> fields;
    fields.reserve(described->size());
    for (ExifFieldFacts& facts : *described)
    {
        ExifField field;
        field.keyword = std::move(facts.keyword);
        field.value = std::move(facts.value);
        field.typeName = std::move(facts.typeName);
        field.tag = facts.tag;
        field.tagName = std::move(facts.tagName);
        field.tagLabel = std::move(facts.tagLabel);
        field.tagDescription = std::move(facts.tagDescription);
        field.count = facts.count;
        fields.push_back(std::move(field));
    }
    return fields;
}
} // namespace an
