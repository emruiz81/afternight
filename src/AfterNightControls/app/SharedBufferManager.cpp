// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The extension buffer seam's implementation: the format an export states, the frame an import reads, the copies in
/// both directions, and the JSON sidecar that carries the cards and the solution across the process boundary.
#include "stdafx.hpp"

#include "app/SharedBufferManager.h"

#include "../../AfterNightExtensionProtocol/SharedBufferFileMapping.h"
#include "app/ApplicationStorage.h"
#include "app/PlateSolveTypes.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/FitsHeaderCards.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace an::ui::extensions
{

namespace
{

/// A refusal with a message for a person.
[[nodiscard]] an::Status refuse(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The protocol's spelling of its representation, for messages.
[[nodiscard]] std::string protocolTypeName(const SharedBufferPixelType type)
{
    return std::string(sharedBufferPixelTypeName(type));
}

/// The protocol's `SharedBufferFileSpec` of an allocation specification.
[[nodiscard]] SharedBufferFileSpec toFileSpec(const SharedImageBufferSpec& spec)
{
    SharedBufferFileSpec file;
    file.session_id = spec.sessionId;
    file.buffer_id = spec.bufferId;
    file.symbolic_name = spec.symbolicName;
    file.width = spec.width;
    file.height = spec.height;
    file.channels = spec.channels;
    file.pixel_type = spec.pixelType;
    file.pixel_format = spec.pixelFormat;
    file.color_space = spec.colorSpace;
    file.access_mode = spec.accessMode;
    file.buffer_role = spec.bufferRole;
    file.offset_x = spec.offsetX;
    file.offset_y = spec.offsetY;
    file.source_width = spec.sourceWidth;
    file.source_height = spec.sourceHeight;
    file.metadata_sidecar = spec.metadataSidecar;
    return file;
}

/// The Bayer pattern seen from a pixel of a mosaic: the pattern whose block at the origin is the mosaic's at that
/// pixel.
[[nodiscard]] std::optional<SharedBufferPixelFormat> bayerFormatSeenFrom(const an::CfaDescriptor& cfa,
                                                                         const std::uint32_t x, const std::uint32_t y)
{
    const an::CfaColor topLeft = cfa.colorAt(x, y);
    const an::CfaColor topRight = cfa.colorAt(x + 1u, y);
    if (topLeft == an::CfaColor::Red)
    {
        return SharedBufferPixelFormat::BayerRGGB;
    }
    if (topLeft == an::CfaColor::Blue)
    {
        return SharedBufferPixelFormat::BayerBGGR;
    }
    if (topRight == an::CfaColor::Red)
    {
        return SharedBufferPixelFormat::BayerGRBG;
    }
    if (topRight == an::CfaColor::Blue)
    {
        return SharedBufferPixelFormat::BayerGBRG;
    }
    return std::nullopt;
}

/// The Bayer pattern a protocol pixel format names, or none for mono and RGB.
[[nodiscard]] an::CfaPattern cfaPatternOf(const SharedBufferPixelFormat format) noexcept
{
    switch (format)
    {
    case SharedBufferPixelFormat::BayerRGGB:
        return an::CfaPattern::Rggb;
    case SharedBufferPixelFormat::BayerBGGR:
        return an::CfaPattern::Bggr;
    case SharedBufferPixelFormat::BayerGRBG:
        return an::CfaPattern::Grbg;
    case SharedBufferPixelFormat::BayerGBRG:
        return an::CfaPattern::Gbrg;
    case SharedBufferPixelFormat::Mono:
    case SharedBufferPixelFormat::RGB:
        break;
    }
    return an::CfaPattern::None;
}

/// Whether a colour image's samples are encoded rather than linear, read from its semantics first, its processing-state
/// cards second and its description last: a stretch the product runs states no transfer function and records the
/// samples nonlinear in the cards.
[[nodiscard]] bool isEncoded(const an::ImageDescriptor& descriptor, const an::ImageMetadata& metadata)
{
    switch (metadata.semantics().transferFunction())
    {
    case an::TransferFunction::Srgb:
    case an::TransferFunction::Rec709:
    case an::TransferFunction::PureGamma:
        return true;
    case an::TransferFunction::Linear:
        return false;
    case an::TransferFunction::Unspecified:
        break;
    }
    if (an::readProcessingStateCards(metadata.codec()).linearity == an::RecordedLinearity::Nonlinear)
    {
        return true;
    }
    return descriptor.colorSpace() == an::ColorSpace::DisplayRgb ||
           descriptor.numericDomain() == an::NumericDomain::EncodedNonlinear;
}

/// The layout of a buffer's bytes as the protocol strides them: one plane per channel, rows at `row_stride`, planes at
/// `plane_stride`, aligned to the sample.
[[nodiscard]] std::optional<an::ImageLayout> bufferLayoutOf(const SharedBufferDescriptor& descriptor,
                                                            const an::ImageExtent& extent)
{
    const std::uint32_t elementSize = an::sampleTypeSizeBytes(sampleTypeOf(descriptor.pixel_type));
    const an::ImageLayoutRequest request{.extent = extent,
                                         .elementSizeBytes = elementSize,
                                         .samplesPerPixel = 1u,
                                         .planeCount = extent.channelCount(),
                                         .rowAlignmentBytes = elementSize};
    return an::ImageLayout::createWithStrides(
        request, descriptor.row_stride, descriptor.plane_stride,
        an::viewGeometryLimits(extent.width(), extent.height(), extent.channelCount()));
}

// ---- The sidecar.

[[nodiscard]] std::filesystem::path sidecarPathOf(const std::filesystem::path& bufferPath)
{
    std::filesystem::path sidecar = bufferPath;
    sidecar += ".metadata.json";
    return sidecar;
}

/// The keys of the sidecar that hold structured records rather than one card.
[[nodiscard]] bool isStructuredSidecarKey(const QString& key) noexcept
{
    return key == QStringLiteral("fits_header") || key == QStringLiteral("fits_cards") ||
           key == QStringLiteral("exif_header") || key == QStringLiteral("exif_entries") ||
           key == QStringLiteral("astrometry");
}

/// The comment a reader gives each card that continues a field longer than one card: an Exif field, an XISF
/// property.
constexpr std::string_view kContinuedComment = "continued";

/// Whether a card is an EXIF field: its keyword is spelled `Exif.<group>.<tag>`.
[[nodiscard]] bool isExifCard(const an::CodecCard& card) noexcept
{
    return card.keyword().starts_with("Exif.");
}

/// Whether a card is a header card the sidecar's `fits_header` carries: FITS or XISF, and not an EXIF field.
[[nodiscard]] bool isHeaderCard(const an::CodecCard& card) noexcept
{
    return (card.origin() == an::CodecNamespace::Fits || card.origin() == an::CodecNamespace::Xisf) &&
           !isExifCard(card);
}

void insertFinite(QJsonObject& object, const QString& key, const double value)
{
    if (std::isfinite(value))
    {
        object.insert(key, value);
    }
}

[[nodiscard]] QJsonArray polynomialToJson(const an::SipPolynomial& polynomial)
{
    QJsonArray rows;
    for (const auto& row : polynomial.coefficients)
    {
        QJsonArray values;
        for (const double value : row)
        {
            values.push_back(std::isfinite(value) ? QJsonValue(value) : QJsonValue());
        }
        rows.push_back(values);
    }
    return rows;
}

[[nodiscard]] QJsonObject distortionToJson(const an::SipDistortion& distortion)
{
    QJsonObject object;
    object.insert(QStringLiteral("has_forward_model"), distortion.hasForwardModel());
    object.insert(QStringLiteral("has_inverse_model"), distortion.hasInverseModel());
    object.insert(QStringLiteral("a_order"), static_cast<int>(distortion.forwardX().order));
    object.insert(QStringLiteral("b_order"), static_cast<int>(distortion.forwardY().order));
    object.insert(QStringLiteral("ap_order"), static_cast<int>(distortion.inverseX().order));
    object.insert(QStringLiteral("bp_order"), static_cast<int>(distortion.inverseY().order));
    // The record states the reference pixel as the CRPIX cards beside it do, from the first pixel's centre at one; the
    // engine's own coordinates put that centre at one half.
    const double crpix1 = distortion.referencePixel().x + 0.5;
    const double crpix2 = distortion.referencePixel().y + 0.5;
    insertFinite(object, QStringLiteral("crpix1"), crpix1);
    insertFinite(object, QStringLiteral("crpix2"), crpix2);
    object.insert(QStringLiteral("A"), polynomialToJson(distortion.forwardX()));
    object.insert(QStringLiteral("B"), polynomialToJson(distortion.forwardY()));
    object.insert(QStringLiteral("AP"), polynomialToJson(distortion.inverseX()));
    object.insert(QStringLiteral("BP"), polynomialToJson(distortion.inverseY()));
    QString description;
    if (distortion.hasForwardModel())
    {
        description = QStringLiteral("SIP forward A_ORDER=%1, B_ORDER=%2")
                          .arg(distortion.forwardX().order)
                          .arg(distortion.forwardY().order);
    }
    if (distortion.hasInverseModel())
    {
        if (!description.isEmpty())
        {
            description += QStringLiteral(", ");
        }
        description += QStringLiteral("inverse AP_ORDER=%1, BP_ORDER=%2")
                           .arg(distortion.inverseX().order)
                           .arg(distortion.inverseY().order);
    }
    description += QStringLiteral(", CRPIX=(%1, %2)").arg(crpix1, 0, 'f', 1).arg(crpix2, 0, 'f', 1);
    object.insert(QStringLiteral("description"), description);
    return object;
}

/// The value of a header card by keyword, or empty.
[[nodiscard]] QString headerCardValue(const std::span<const an::CodecCard> cards, const std::string_view keyword)
{
    for (const an::CodecCard& card : cards)
    {
        if (isHeaderCard(card) && card.keyword() == keyword)
        {
            return QString::fromStdString(card.value());
        }
    }
    return QString();
}

/// A header card's text without the quotes a FITS string is written in - `'ASTAP'` reads `ASTAP`, a doubled quote one
/// quote - as the `astrometry` record states its texts; a value that is no string is returned as it is.
[[nodiscard]] QString unquotedHeaderText(const QString& value)
{
    QString text = value.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\'')))
    {
        text = text.mid(1, text.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return text.trimmed();
}

/// The sidecar's `astrometry` record of an image's solution, read from its cards; empty when the cards hold none. Its
/// texts are the cards' values unquoted; the `fits_header` map beside it keeps each card as written.
[[nodiscard]] QJsonObject astrometryToJson(const an::CodecMetadata& codec, const std::uint32_t width,
                                           const std::uint32_t height)
{
    QJsonObject object;
    const std::span<const an::CodecCard> cards = codec.cards();
    if (an::checkWcsCards(cards) != an::WcsCardError::None)
    {
        return object;
    }
    const an::Result<an::WcsSolution> solved = an::ui::readImageWcsSolution(cards, width, height);
    if (!solved.hasValue())
    {
        return object;
    }
    const an::WcsSolution& solution = *solved;

    object.insert(QStringLiteral("has_solution"), true);
    object.insert(QStringLiteral("width"), static_cast<int>(solution.imageWidth()));
    object.insert(QStringLiteral("height"), static_cast<int>(solution.imageHeight()));
    insertFinite(object, QStringLiteral("image_scale_arcsec_per_pixel"), solution.meanPixelScaleArcsecPerPixel());
    insertFinite(object, QStringLiteral("rotation_degrees"), solution.orientationDegrees());
    if (const an::Result<an::SkyCoordinate> center = solution.fieldCenter(); center.hasValue())
    {
        insertFinite(object, QStringLiteral("field_center_ra_deg"), center->rightAscensionDegrees);
        insertFinite(object, QStringLiteral("field_center_dec_deg"), center->declinationDegrees);
    }
    insertFinite(object, QStringLiteral("crval_ra_deg"), solution.referencePoint().rightAscensionDegrees);
    insertFinite(object, QStringLiteral("crval_dec_deg"), solution.referencePoint().declinationDegrees);
    const an::WcsFieldOfView fieldOfView = solution.fieldOfViewArcmin();
    insertFinite(object, QStringLiteral("field_of_view_width_arcmin"), fieldOfView.widthArcmin);
    insertFinite(object, QStringLiteral("field_of_view_height_arcmin"), fieldOfView.heightArcmin);

    const QString ctype1 = unquotedHeaderText(headerCardValue(cards, "CTYPE1"));
    const QString ctype2 = unquotedHeaderText(headerCardValue(cards, "CTYPE2"));
    if (!ctype1.isEmpty() || !ctype2.isEmpty())
    {
        object.insert(QStringLiteral("projection"),
                      QJsonObject{{QStringLiteral("ctype1"), ctype1}, {QStringLiteral("ctype2"), ctype2}});
    }
    if (const QString equinox = unquotedHeaderText(headerCardValue(cards, "EQUINOX")); !equinox.isEmpty())
    {
        object.insert(QStringLiteral("equinox"), equinox);
    }
    QString solver = unquotedHeaderText(headerCardValue(cards, an::ui::kPlateSolveSolverKeyword));
    if (solver.isEmpty())
    {
        solver = unquotedHeaderText(headerCardValue(cards, "PLTSLVR"));
    }
    if (!solver.isEmpty())
    {
        object.insert(QStringLiteral("solver"), solver);
    }
    const an::SipDistortion& distortion = solution.distortion();
    if (distortion.hasForwardModel() || distortion.hasInverseModel())
    {
        object.insert(QStringLiteral("sip"), distortionToJson(distortion));
    }
    return object;
}

/// The header of a rectangle of an image: its solution moved with the samples, as a crop moves it, or none when the
/// cards state none that can be moved.
[[nodiscard]] an::Result<an::CodecMetadata> regionHeader(const an::CodecMetadata& codec, const an::ImageExtent& whole,
                                                         const QRect& region)
{
    const std::span<const an::CodecCard> cards = codec.cards();
    if (an::checkWcsCards(cards) == an::WcsCardError::None)
    {
        const an::Result<an::WcsSolution> solved = an::ui::readImageWcsSolution(cards, whole.width(), whole.height());
        if (solved.hasValue())
        {
            const an::Result<an::WcsSolution> moved = an::cropWcsSolution(
                *solved, static_cast<std::uint32_t>(region.x()), static_cast<std::uint32_t>(region.y()),
                static_cast<std::uint32_t>(region.width()), static_cast<std::uint32_t>(region.height()));
            if (moved.hasValue())
            {
                return an::withWcsCards(codec, *moved);
            }
        }
    }
    return an::withoutWcsCards(codec);
}

/// The Exif fields a codec's cards state, whole and described, in key order as the sidecar lists them. A field the
/// metadata library cannot describe is still listed, by its card.
[[nodiscard]] std::vector<an::ExifField> sidecarExifFields(const an::CodecMetadata& codec)
{
    std::vector<an::ExifField> fields;
    if (an::Result<std::vector<an::ExifField>> described = an::describeExifFields(codec.cards()); described.hasValue())
    {
        fields = std::move(*described);
    }
    else
    {
        for (const an::CodecCard& card : codec.cards())
        {
            if (isExifCard(card))
            {
                fields.push_back(
                    an::ExifField{.keyword = card.keyword(), .value = card.value(), .typeName = card.comment()});
            }
        }
    }
    std::stable_sort(fields.begin(), fields.end(), [](const an::ExifField& left, const an::ExifField& right) {
        return left.keyword < right.keyword;
    });
    return fields;
}

/// One Exif field's entry: its key and value, the library's type, names and description where it states them, and its
/// tag number and component count.
[[nodiscard]] QJsonObject exifEntry(const an::ExifField& field)
{
    QJsonObject entry{{QStringLiteral("key"), QString::fromStdString(field.keyword)},
                      {QStringLiteral("value"), QString::fromStdString(field.value)}};
    const auto insertText = [&entry](const QString& key, const std::string& text) {
        if (!text.empty())
        {
            entry.insert(key, QString::fromStdString(text));
        }
    };
    insertText(QStringLiteral("tag_name"), field.tagName);
    insertText(QStringLiteral("tag_label"), field.tagLabel);
    insertText(QStringLiteral("tag_description"), field.tagDescription);
    insertText(QStringLiteral("type_name"), field.typeName);
    entry.insert(QStringLiteral("tag"), static_cast<int>(field.tag));
    entry.insert(QStringLiteral("count"), static_cast<double>(field.count));
    return entry;
}

/// What a lent buffer is, as the header cards that describe a file's structure state it.
struct BufferStructure
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;
    SharedBufferPixelType pixelType = SharedBufferPixelType::Float32;
};

/// The structural cards of a buffer, as its header states them for the samples it lends: a FITS primary image of the
/// buffer's representation, two axes for one plane and three for three, and no scaling - the samples are what they
/// are. Each keeps the comment the image's own card gave it.
[[nodiscard]] std::vector<an::CodecCard> structuralCards(const BufferStructure& structure,
                                                         const std::span<const an::CodecCard> cards)
{
    const auto bitpix = [](const SharedBufferPixelType type) {
        switch (type)
        {
        case SharedBufferPixelType::UInt8:
            return "8";
        case SharedBufferPixelType::UInt16:
            return "16";
        case SharedBufferPixelType::Float32:
            return "-32";
        case SharedBufferPixelType::Float64:
            return "-64";
        }
        return "-32";
    };
    std::vector<std::pair<std::string, std::string>> stated = {{"SIMPLE", "T"},
                                                               {"BITPIX", bitpix(structure.pixelType)},
                                                               {"NAXIS", structure.channels > 1u ? "3" : "2"},
                                                               {"NAXIS1", std::to_string(structure.width)},
                                                               {"NAXIS2", std::to_string(structure.height)}};
    if (structure.channels > 1u)
    {
        stated.emplace_back("NAXIS3", std::to_string(structure.channels));
    }
    stated.emplace_back("BZERO", "0");
    stated.emplace_back("BSCALE", "1");
    std::vector<an::CodecCard> structural;
    for (const auto& [keyword, value] : stated)
    {
        std::string comment;
        for (const an::CodecCard& card : cards)
        {
            if (isHeaderCard(card) && card.keyword() == keyword)
            {
                comment = card.comment();
                break;
            }
        }
        an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, comment);
        if (card.hasValue())
        {
            structural.push_back(std::move(*card));
        }
    }
    return structural;
}

/// The sidecar document of a lent buffer: `FILENAME`, every header card at the top level and again under `fits_header`
/// and `fits_cards` with the record it is written as - the cards that describe a file's structure stated for the buffer
/// rather than as the file stated them - the EXIF fields under `exif_header` and `exif_entries`, and the solution under
/// `astrometry`.
[[nodiscard]] QJsonObject sidecarDocument(const an::CodecMetadata& codec, const BufferStructure& structure,
                                          const std::string& fileName)
{
    QJsonObject root;
    if (!fileName.empty())
    {
        root.insert(QStringLiteral("FILENAME"), QString::fromStdString(fileName));
    }

    QJsonObject header;
    QJsonArray headerCards;
    const std::span<const an::CodecCard> cards = codec.cards();
    std::vector<an::CodecCard> stated = structuralCards(structure, cards);
    for (const an::CodecCard& card : cards)
    {
        if (!card.keyword().empty() && isHeaderCard(card) && !an::isStructuralFitsKeyword(card.keyword()))
        {
            stated.push_back(card);
        }
    }
    // A field continued across several cards - a long XISF property - is one entry with its value whole, as an
    // extension reads it and as the import continues it again. A FITS card never continues another, and a commentary
    // record whose text is `continued` is one of its own.
    for (std::size_t index = 0u; index < stated.size(); ++index)
    {
        const an::CodecCard& card = stated[index];
        std::string text = card.value();
        std::size_t last = index;
        while (card.origin() == an::CodecNamespace::Xisf && last + 1u < stated.size() &&
               stated[last + 1u].comment() == kContinuedComment && stated[last + 1u].origin() == card.origin() &&
               stated[last + 1u].keyword() == card.keyword())
        {
            text += stated[++last].value();
        }
        const QString key = QString::fromStdString(card.keyword());
        const QString value = QString::fromStdString(text);
        root.insert(key, value);
        header.insert(key, value);
        QJsonObject entry{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}};
        if (!card.comment().empty())
        {
            entry.insert(QStringLiteral("comment"), QString::fromStdString(card.comment()));
        }
        if (const an::Result<std::string> record = an::formatFitsCardRecord(card); last == index && record.hasValue())
        {
            entry.insert(QStringLiteral("raw"), QString::fromStdString(*record));
        }
        headerCards.push_back(entry);
        index = last;
    }
    if (!header.isEmpty())
    {
        root.insert(QStringLiteral("fits_header"), header);
        root.insert(QStringLiteral("fits_cards"), headerCards);
    }

    QJsonObject exifHeader;
    QJsonArray exifEntries;
    for (const an::ExifField& field : sidecarExifFields(codec))
    {
        exifHeader.insert(QString::fromStdString(field.keyword), QString::fromStdString(field.value));
        exifEntries.push_back(exifEntry(field));
    }
    if (!exifHeader.isEmpty())
    {
        root.insert(QStringLiteral("exif_header"), exifHeader);
        root.insert(QStringLiteral("exif_entries"), exifEntries);
    }
    if (const QJsonObject astrometry = astrometryToJson(codec, structure.width, structure.height);
        !astrometry.isEmpty())
    {
        root.insert(QStringLiteral("astrometry"), astrometry);
    }
    return root;
}

[[nodiscard]] an::Status writeSidecar(const QByteArray& bytes, const std::filesystem::path& path)
{
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return refuse(an::ErrorCode::ExternalFailure,
                      "the metadata sidecar cannot be opened for writing: " + path.generic_string());
    }
    if (file.write(bytes) != bytes.size())
    {
        return refuse(an::ErrorCode::ExternalFailure,
                      "the metadata sidecar cannot be written: " + path.generic_string());
    }
    return an::Status::success();
}

/// What a sidecar states: the file name and the cards.
struct SidecarContents
{
    std::string fileName;
    std::vector<an::CodecCard> cards;
};

/// Adds a card to the sidecar's contents once per keyword and origin, except the FITS commentary keywords, which
/// repeat.
[[nodiscard]] an::Status addCard(SidecarContents& contents, const an::CodecNamespace stated, const QString& keyword,
                                 const QString& value, const QString& comment)
{
    const std::string key = keyword.toStdString();
    if (key.empty())
    {
        return an::Status::success();
    }
    // A header key with a colon is an XISF property's identifier, `Observation:Object:Name`, which no FITS keyword
    // spells, so the card goes back to the namespace it came from and an XISF write states it as a property again.
    const an::CodecNamespace origin =
        stated == an::CodecNamespace::Fits && key.find(':') != std::string::npos ? an::CodecNamespace::Xisf : stated;
    const bool commentary = key == "COMMENT" || key == "HISTORY";
    if (!commentary)
    {
        const bool present = std::any_of(contents.cards.begin(), contents.cards.end(), [&](const an::CodecCard& card) {
            return card.origin() == origin && card.keyword() == key;
        });
        if (present)
        {
            return an::Status::success();
        }
    }
    const std::string text = value.toStdString();
    if (text.size() <= an::kMaxCodecCardTextBytes)
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(origin, key, text, comment.toStdString());
        if (!card.hasValue())
        {
            return card.status();
        }
        contents.cards.push_back(std::move(*card));
        return an::Status::success();
    }
    // A field longer than one card holds - an Exif field, an XISF property - continues in the cards after it, as the
    // codec's reader states it: each piece within the bound and never splitting a UTF-8 character, the first carrying
    // the field's type.
    std::size_t offset = 0u;
    while (offset < text.size())
    {
        std::size_t length = (std::min)(an::kMaxCodecCardTextBytes, text.size() - offset);
        while (offset + length < text.size() && length > 0u &&
               (static_cast<unsigned char>(text[offset + length]) & 0xC0u) == 0x80u)
        {
            --length;
        }
        an::Result<an::CodecCard> card =
            an::CodecCard::create(origin, key, text.substr(offset, length),
                                  offset == 0u ? comment.toStdString() : std::string(kContinuedComment));
        if (!card.hasValue())
        {
            return card.status();
        }
        contents.cards.push_back(std::move(*card));
        offset += length;
    }
    return an::Status::success();
}

/// The comment each header entry states for its key, with the value it states it beside.
using EntryComments = std::map<QString, std::pair<QString, QString>>;

/// The comment and value of the first entry of each key, from an array of entries.
[[nodiscard]] EntryComments entryComments(const QJsonArray& entries, const QString& commentKey)
{
    EntryComments comments;
    for (const QJsonValue& value : entries)
    {
        const QJsonObject entry = value.toObject();
        comments.try_emplace(entry.value(QStringLiteral("key")).toString(),
                             entry.value(QStringLiteral("value")).toString(), entry.value(commentKey).toString());
    }
    return comments;
}

/// The comment a key's card takes: the one its entry states, while the value is the one the entry states beside it.
/// An extension that changed a value has said nothing of its comment, and a type that described the old value does
/// not describe the new one.
[[nodiscard]] QString commentFor(const EntryComments& comments, const QString& key, const QString& value)
{
    const auto entry = comments.find(key);
    return entry != comments.end() && entry->second.first == value ? entry->second.second : QString();
}

[[nodiscard]] an::Status addCardsFromObject(SidecarContents& contents, const an::CodecNamespace origin,
                                            const QJsonObject& values, const EntryComments& comments)
{
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        if (!it.value().isString())
        {
            continue;
        }
        const QString value = it.value().toString();
        if (const an::Status added = addCard(contents, origin, it.key(), value, commentFor(comments, it.key(), value));
            added.failed())
        {
            return added;
        }
    }
    return an::Status::success();
}

[[nodiscard]] an::Status addCardsFromEntries(SidecarContents& contents, const an::CodecNamespace origin,
                                             const QJsonArray& entries, const QString& commentKey)
{
    for (const QJsonValue& value : entries)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject entry = value.toObject();
        if (const an::Status added =
                addCard(contents, origin, entry.value(QStringLiteral("key")).toString(),
                        entry.value(QStringLiteral("value")).toString(), entry.value(commentKey).toString());
            added.failed())
        {
            return added;
        }
    }
    return an::Status::success();
}

/// Reads a buffer's sidecar, when the descriptor names one and it exists; a sidecar outside the managed root is
/// refused.
[[nodiscard]] an::Result<SidecarContents> readSidecar(const std::filesystem::path& root,
                                                      const SharedBufferDescriptor& descriptor)
{
    SidecarContents contents;
    if (descriptor.metadata_sidecar.empty())
    {
        return contents;
    }
    const std::filesystem::path path = std::filesystem::path(descriptor.metadata_sidecar).lexically_normal();
    if (!sharedBufferPathIsWithinRoot(root, path))
    {
        return refuse(an::ErrorCode::InvalidArgument, "the metadata sidecar's path is outside the managed root");
    }
    QFile file(QString::fromStdString(path.string()));
    if (!file.exists())
    {
        return contents;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return refuse(an::ErrorCode::ExternalFailure, "the metadata sidecar cannot be read: " + path.generic_string());
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return refuse(an::ErrorCode::InvalidData,
                      "the metadata sidecar is not a JSON object: " + path.generic_string());
    }

    const QJsonObject object = document.object();
    const EntryComments headerComments =
        entryComments(object.value(QStringLiteral("fits_cards")).toArray(), QStringLiteral("comment"));
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!it.value().isString() || isStructuredSidecarKey(it.key()))
        {
            continue;
        }
        if (it.key() == QStringLiteral("FILENAME"))
        {
            contents.fileName = it.value().toString().toStdString();
            continue;
        }
        const QString value = it.value().toString();
        if (const an::Status added = addCard(contents, an::CodecNamespace::Fits, it.key(), value,
                                             commentFor(headerComments, it.key(), value));
            added.failed())
        {
            return added;
        }
    }
    if (const an::Status added = addCardsFromObject(
            contents, an::CodecNamespace::Fits, object.value(QStringLiteral("fits_header")).toObject(), headerComments);
        added.failed())
    {
        return added;
    }
    if (const an::Status added =
            addCardsFromEntries(contents, an::CodecNamespace::Fits,
                                object.value(QStringLiteral("fits_cards")).toArray(), QStringLiteral("comment"));
        added.failed())
    {
        return added;
    }
    // The Exif entries before the Exif header object, so a field keeps the type its entry states.
    if (const an::Status added =
            addCardsFromEntries(contents, an::CodecNamespace::Tiff,
                                object.value(QStringLiteral("exif_entries")).toArray(), QStringLiteral("type_name"));
        added.failed())
    {
        return added;
    }
    if (const an::Status added = addCardsFromObject(contents, an::CodecNamespace::Tiff,
                                                    object.value(QStringLiteral("exif_header")).toObject(), {});
        added.failed())
    {
        return added;
    }
    return contents;
}

/// The metadata of an imported buffer: the sidecar's cards, the acquisition record and the solution projected from
/// them, the semantics its colour space states, and a lineage that names the buffer.
[[nodiscard]] an::Result<an::ImageMetadata> importedMetadata(const SharedBufferDescriptor& descriptor,
                                                             const an::ImageExtent& extent, SidecarContents&& sidecar)
{
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(sidecar.cards));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    const an::AcquisitionProjection projection = an::projectAcquisition(*codec);
    an::Result<an::AcquisitionMetadata> acquisition = an::AcquisitionMetadata::create(projection.acquisition);
    if (!acquisition.hasValue())
    {
        return acquisition.status();
    }
    an::AstrometricSolution astrometry;
    if (an::checkWcsCards(codec->cards()) == an::WcsCardError::None)
    {
        const an::Result<an::WcsSolution> solution =
            an::ui::readImageWcsSolution(codec->cards(), extent.width(), extent.height());
        if (solution.hasValue())
        {
            an::Result<an::AstrometricSolution> summary = solution->toAstrometricSolution();
            if (!summary.hasValue())
            {
                return summary.status();
            }
            astrometry = std::move(*summary);
        }
    }
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(
        an::ImageSemanticsRequest{.transferFunction = transferFunctionOf(descriptor.color_space)});
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const std::string name = descriptor.symbolic_name.empty() ? descriptor.buffer_id : descriptor.symbolic_name;
    an::Result<an::Provenance> provenance = an::Provenance::create(an::ProvenanceSourceKind::Derived, name);
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(
        "shared buffer import", std::string(sharedBufferRoleName(descriptor.buffer_role)) + " buffer, " +
                                    std::string(sharedBufferPixelFormatName(descriptor.pixel_format)) + " " +
                                    protocolTypeName(descriptor.pixel_type) + ", " +
                                    std::string(sharedBufferColorSpaceName(descriptor.color_space)));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    provenance = provenance->appended(*entry);
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    return an::ImageMetadata::create(std::move(*acquisition), std::move(*semantics), std::move(astrometry),
                                     std::move(*provenance), std::move(*codec), std::nullopt);
}

// ---- The copies.

/// Copies the rows of a rectangle of a typed image into a frame's planes.
template <typename SampleT>
[[nodiscard]] an::Status copyRowsToFrame(const an::Image<SampleT>& image, const QRect& region, an::RawFrame& frame)
{
    const std::optional<an::ImageView<const SampleT>> view = image.view();
    if (!view.has_value())
    {
        return refuse(an::ErrorCode::NotInitialized, "the image holds no samples");
    }
    const auto x0 = static_cast<std::uint32_t>(region.x());
    const auto y0 = static_cast<std::uint32_t>(region.y());
    const auto width = static_cast<std::size_t>(region.width());
    const auto height = static_cast<std::uint32_t>(region.height());
    for (std::uint32_t plane = 0u; plane < view->planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const std::optional<std::span<const SampleT>> source = view->row(plane, y0 + y);
            const std::optional<std::span<std::byte>> destination = frame.mutableRow(plane, y);
            if (!source.has_value() || !destination.has_value() || source->size() < x0 + width ||
                destination->size() < width * sizeof(SampleT))
            {
                return refuse(an::ErrorCode::Internal, "the export's rows do not agree with the buffer's");
            }
            const std::span<const std::byte> bytes = std::as_bytes(source->subspan(x0, width));
            std::copy(bytes.begin(), bytes.end(), destination->begin());
        }
    }
    return an::Status::success();
}

/// Copies a frame's planes into a new typed image of a context, row by row.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
copyFrameToImage(const an::EngineContext& engine, const an::ImageDescriptor& descriptor, const an::RawFrame& frame,
                 an::ImageMetadata&& metadata)
{
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(engine, descriptor, an::StorageArrangement::Planar,
                                   an::StorageInitialization::Uninitialized, std::move(metadata));
    if (!image.hasValue())
    {
        return image.status();
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        if (!access.has_value())
        {
            return refuse(an::ErrorCode::NotInitialized, "the imported image cannot be written");
        }
        const an::ImageView<SampleT>& view = access->view();
        const std::size_t rowBytes = static_cast<std::size_t>(view.width()) * sizeof(SampleT);
        for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < view.height(); ++y)
            {
                const std::optional<std::span<const std::byte>> source = frame.row(plane, y);
                const std::optional<std::span<SampleT>> destination = view.row(plane, y);
                if (!source.has_value() || !destination.has_value() || source->size() < rowBytes)
                {
                    return refuse(an::ErrorCode::Internal, "the import's rows do not agree with the buffer's");
                }
                const std::span<std::byte> bytes = std::as_writable_bytes(*destination);
                std::copy(source->begin(), source->begin() + static_cast<std::ptrdiff_t>(rowBytes), bytes.begin());
            }
        }
    }
    return an::consumer::AnyImage{std::move(*image)};
}

} // namespace

// ---- SharedImageBuffer.

SharedImageBuffer::SharedImageBuffer() noexcept = default;

SharedImageBuffer::~SharedImageBuffer() = default;

SharedImageBuffer::SharedImageBuffer(SharedImageBuffer&& other) noexcept = default;

SharedImageBuffer& SharedImageBuffer::operator=(SharedImageBuffer&& other) noexcept = default;

bool SharedImageBuffer::valid() const noexcept
{
    return m_mapping != nullptr && m_mapping->valid();
}

const SharedBufferDescriptor& SharedImageBuffer::descriptor() const noexcept
{
    return m_descriptor;
}

const std::filesystem::path& SharedImageBuffer::filePath() const noexcept
{
    return m_filePath;
}

std::uint64_t SharedImageBuffer::mappedSize() const noexcept
{
    return m_mapping != nullptr ? m_mapping->mapped_size() : 0u;
}

std::byte* SharedImageBuffer::bytes() noexcept
{
    return m_mapping != nullptr ? m_mapping->bytes() : nullptr;
}

const std::byte* SharedImageBuffer::bytes() const noexcept
{
    return m_mapping != nullptr ? m_mapping->bytes() : nullptr;
}

void SharedImageBuffer::release() noexcept
{
    m_mapping.reset();
}

std::shared_ptr<void> SharedImageBuffer::mappingToken() const noexcept
{
    return m_mapping;
}

const QByteArray& SharedImageBuffer::sidecarBytes() const noexcept
{
    return m_sidecarBytes;
}

std::uint64_t SharedImageBuffer::sidecarWrites() const noexcept
{
    return m_sidecarWrites;
}

// ---- The format and the frame description.

an::Result<SharedBufferPixelType> sharedBufferPixelTypeOf(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::UInt8:
        return SharedBufferPixelType::UInt8;
    case an::SampleType::UInt16:
        return SharedBufferPixelType::UInt16;
    case an::SampleType::Float32:
        return SharedBufferPixelType::Float32;
    case an::SampleType::Float64:
        return SharedBufferPixelType::Float64;
    case an::SampleType::Int8:
    case an::SampleType::Int16:
    case an::SampleType::Int32:
    case an::SampleType::UInt32:
        break;
    }
    return refuse(an::ErrorCode::Unsupported,
                  std::string("the shared-buffer protocol carries UInt8, UInt16, Float32 and Float64 samples, not ") +
                      an::sampleTypeName(type));
}

an::SampleType sampleTypeOf(const SharedBufferPixelType type) noexcept
{
    switch (type)
    {
    case SharedBufferPixelType::UInt8:
        return an::SampleType::UInt8;
    case SharedBufferPixelType::UInt16:
        return an::SampleType::UInt16;
    case SharedBufferPixelType::Float32:
        return an::SampleType::Float32;
    case SharedBufferPixelType::Float64:
        return an::SampleType::Float64;
    }
    return an::SampleType::Float32;
}

an::Result<SharedBufferImageFormat> sharedBufferFormatOf(const an::ImageDescriptor& descriptor,
                                                         const an::ImageMetadata& metadata, const QPoint origin)
{
    an::Result<SharedBufferPixelType> pixelType = sharedBufferPixelTypeOf(descriptor.sampleType());
    if (!pixelType.hasValue())
    {
        return pixelType.status();
    }
    SharedBufferImageFormat format;
    format.pixelType = *pixelType;
    switch (descriptor.channelSchema())
    {
    case an::ChannelSchema::Mono:
        format.colorSpace = SharedBufferColorSpace::Grayscale;
        if (descriptor.isMosaiced())
        {
            if (descriptor.cfa().pattern() == an::CfaPattern::XTrans)
            {
                return refuse(an::ErrorCode::Unsupported, "the shared-buffer protocol names the four Bayer patterns "
                                                          "and no X-Trans mosaic");
            }
            const std::optional<SharedBufferPixelFormat> bayer =
                bayerFormatSeenFrom(descriptor.cfa(), static_cast<std::uint32_t>((std::max)(0, origin.x())),
                                    static_cast<std::uint32_t>((std::max)(0, origin.y())));
            if (!bayer.has_value())
            {
                return refuse(an::ErrorCode::Unsupported, std::string("the mosaic's pattern ") +
                                                              an::cfaPatternName(descriptor.cfa().pattern()) +
                                                              " is not one the shared-buffer protocol names");
            }
            format.pixelFormat = *bayer;
        }
        else
        {
            format.pixelFormat = SharedBufferPixelFormat::Mono;
        }
        return format;
    case an::ChannelSchema::Rgb:
        format.pixelFormat = SharedBufferPixelFormat::RGB;
        format.colorSpace =
            isEncoded(descriptor, metadata) ? SharedBufferColorSpace::SRGB : SharedBufferColorSpace::Linear;
        return format;
    case an::ChannelSchema::Rgba:
    case an::ChannelSchema::Xyz:
    case an::ChannelSchema::Lab:
    case an::ChannelSchema::Lch:
    case an::ChannelSchema::Hsl:
    case an::ChannelSchema::Hsv:
    case an::ChannelSchema::Hsi:
    case an::ChannelSchema::YCbCr:
    case an::ChannelSchema::Named:
        break;
    }
    return refuse(an::ErrorCode::Unsupported,
                  std::string("the shared-buffer protocol carries mono and RGB images, not ") +
                      an::channelSchemaName(descriptor.channelSchema()));
}

an::Result<an::RawFrameDescriptor> frameDescriptorOf(const SharedBufferDescriptor& descriptor)
{
    if (descriptor.width <= 0 || descriptor.height <= 0 || descriptor.channels <= 0)
    {
        return refuse(an::ErrorCode::InvalidArgument, "a shared buffer's dimensions must be positive");
    }
    const auto width = static_cast<std::uint32_t>(descriptor.width);
    const auto height = static_cast<std::uint32_t>(descriptor.height);
    const auto channels = static_cast<std::uint32_t>(descriptor.channels);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::viewGeometryLimits(width, height, channels));
    if (!extent.has_value())
    {
        return refuse(an::ErrorCode::InvalidArgument, "a shared buffer's dimensions describe no image");
    }

    const bool colour = descriptor.pixel_format == SharedBufferPixelFormat::RGB;
    if (colour && channels != 3u)
    {
        return refuse(an::ErrorCode::InvalidArgument, "an RGB shared buffer holds three channels");
    }
    if (!colour && channels != 1u)
    {
        return refuse(an::ErrorCode::InvalidArgument, "a mono or Bayer shared buffer holds one channel");
    }
    if (colour && descriptor.color_space == SharedBufferColorSpace::Grayscale)
    {
        return refuse(an::ErrorCode::Unsupported, "a three-channel buffer in the Grayscale colour space describes no "
                                                  "image");
    }

    an::ColorSpace colorSpace = an::ColorSpace::Unspecified;
    an::NumericDomain domain = an::NumericDomain::NormalizedLinear;
    switch (descriptor.color_space)
    {
    case SharedBufferColorSpace::Linear:
        colorSpace = colour ? an::ColorSpace::LinearRgb : an::ColorSpace::Unspecified;
        break;
    case SharedBufferColorSpace::SRGB:
        colorSpace = colour ? an::ColorSpace::DisplayRgb : an::ColorSpace::Unspecified;
        domain = an::NumericDomain::EncodedNonlinear;
        break;
    case SharedBufferColorSpace::Grayscale:
        break;
    }

    an::CfaDescriptor cfa;
    if (const an::CfaPattern pattern = cfaPatternOf(descriptor.pixel_format); pattern != an::CfaPattern::None)
    {
        const std::optional<an::CfaDescriptor> mosaic = an::CfaDescriptor::create(pattern, 0u, 0u);
        if (!mosaic.has_value())
        {
            return refuse(an::ErrorCode::Internal, "a Bayer pattern at the origin describes a mosaic");
        }
        cfa = *mosaic;
    }

    const an::RawFrameDescriptorRequest request{
        .extent = *extent,
        .sampleFormat = an::rawSampleFormatOf(sampleTypeOf(descriptor.pixel_type)),
        .byteOrder = an::ByteOrder::Native,
        .channelSchema = colour ? an::ChannelSchema::Rgb : an::ChannelSchema::Mono,
        .colorSpace = colorSpace,
        .numericDomain = domain,
        .cfa = cfa,
        .coordinateOrigin = an::CoordinateOrigin::TopLeft,
        .pixelAspectRatio = 1.0};
    const std::optional<an::RawFrameDescriptor> frame = an::RawFrameDescriptor::create(request);
    if (!frame.has_value())
    {
        return refuse(an::ErrorCode::Unsupported,
                      std::string("the buffer's ") + std::string(sharedBufferPixelFormatName(descriptor.pixel_format)) +
                          " " + protocolTypeName(descriptor.pixel_type) + " " +
                          std::string(sharedBufferColorSpaceName(descriptor.color_space)) +
                          " description names no image (" +
                          an::descriptorErrorDescription(an::RawFrameDescriptor::check(request)) + ")");
    }
    return *frame;
}

an::TransferFunction transferFunctionOf(const SharedBufferColorSpace colorSpace) noexcept
{
    switch (colorSpace)
    {
    case SharedBufferColorSpace::Linear:
        return an::TransferFunction::Linear;
    case SharedBufferColorSpace::SRGB:
        return an::TransferFunction::Srgb;
    case SharedBufferColorSpace::Grayscale:
        break;
    }
    return an::TransferFunction::Unspecified;
}

// ---- SharedBufferManager.

SharedBufferManager::SharedBufferManager(std::filesystem::path rootPath) : m_rootPath(std::move(rootPath)) {}

std::filesystem::path SharedBufferManager::defaultSharedBufferRootPath()
{
    QString base = applicationLocalDataPath();
    if (base.isEmpty())
    {
        base = QDir::tempPath() + QStringLiteral("/AfterNight");
    }
    return std::filesystem::path((base + QStringLiteral("/SharedBuffers")).toStdString());
}

const std::filesystem::path& SharedBufferManager::rootPath() const noexcept
{
    return m_rootPath;
}

namespace
{

/// A frame over a buffer's mapping, writable when asked: the export writes the mapping it allocated whatever
/// access the extension is given, and a borrow of an opened buffer writes only what its descriptor allows.
[[nodiscard]] an::Result<an::RawFrame> borrowMapping(SharedImageBuffer& buffer, const bool writable)
{
    if (!buffer.valid())
    {
        return refuse(an::ErrorCode::NotInitialized, "the buffer holds no mapping to borrow");
    }
    an::Result<an::RawFrameDescriptor> described = frameDescriptorOf(buffer.descriptor());
    if (!described.hasValue())
    {
        return described.status();
    }
    const std::optional<an::ImageLayout> layout = bufferLayoutOf(buffer.descriptor(), described->extent());
    if (!layout.has_value())
    {
        return refuse(an::ErrorCode::InvalidArgument, "the buffer's strides describe no layout of its image");
    }
    an::Result<an::SampleBuffer> bytes = an::SampleBuffer::external(
        an::SampleBufferOwnership::Mapped, buffer.bytes(), buffer.mappedSize(), buffer.mappingToken(), writable);
    if (!bytes.hasValue())
    {
        return bytes.status();
    }
    return an::RawFrame::create(*described, *layout, std::move(*bytes));
}

/// What an export of an image, or of a rectangle of it, fills: the rectangle, whether it is less than the whole image,
/// and the buffer it needs, stated as the export states it.
struct ExportPlan
{
    QRect region;
    QRect whole;
    bool cropped = false;
    SharedImageBufferSpec spec;
};

[[nodiscard]] an::Result<ExportPlan> planExport(const an::ImageDescriptor& described, const an::ImageMetadata& metadata,
                                                const ImageExportRequest& request)
{
    const an::ImageExtent& extent = described.extent();
    ExportPlan plan;
    plan.whole = QRect(0, 0, static_cast<int>(extent.width()), static_cast<int>(extent.height()));
    plan.region = plan.whole;
    if (request.region.has_value())
    {
        plan.region = request.region->normalized().intersected(plan.whole);
        if (plan.region.isEmpty())
        {
            return refuse(an::ErrorCode::InvalidArgument, "the exported rectangle lies outside the image");
        }
    }
    plan.cropped = plan.region != plan.whole;

    an::Result<SharedBufferImageFormat> format = sharedBufferFormatOf(described, metadata, plan.region.topLeft());
    if (!format.hasValue())
    {
        return format.status();
    }
    SharedImageBufferSpec& spec = plan.spec;
    spec.sessionId = request.sessionId;
    spec.bufferId = request.bufferId;
    spec.symbolicName = request.symbolicName;
    spec.width = plan.region.width();
    spec.height = plan.region.height();
    spec.channels = static_cast<int>(extent.channelCount());
    spec.pixelType = format->pixelType;
    spec.pixelFormat = format->pixelFormat;
    spec.colorSpace = format->colorSpace;
    spec.accessMode = request.accessMode;
    spec.bufferRole = request.role;
    if (plan.cropped)
    {
        spec.offsetX = plan.region.x();
        spec.offsetY = plan.region.y();
        spec.sourceWidth = plan.whole.width();
        spec.sourceHeight = plan.whole.height();
    }
    return plan;
}

/// The bytes a buffer's sidecar is written with.
[[nodiscard]] QByteArray renderedSidecar(const QJsonObject& document)
{
    return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

} // namespace

an::Result<SharedImageBuffer> SharedBufferManager::allocateBuffer(const SharedImageBufferSpec& spec) const
{
    const SharedBufferFileMapper mapper(m_rootPath);
    auto mapping = std::make_shared<SharedBufferFileMapping>();
    std::string error;
    if (!mapper.allocateBuffer(toFileSpec(spec), mapping.get(), &error))
    {
        const bool external = error.starts_with("Failed to");
        return refuse(external ? an::ErrorCode::ExternalFailure : an::ErrorCode::InvalidArgument, std::move(error));
    }
    SharedImageBuffer buffer;
    buffer.m_descriptor = mapping->descriptor();
    buffer.m_filePath = mapping->file_path();
    buffer.m_mapping = std::move(mapping);
    return buffer;
}

an::Result<SharedImageBuffer> SharedBufferManager::exportImage(const an::consumer::AnyImage& image,
                                                               const ImageExportRequest& request) const
{
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& typed) { return typed.descriptor(); }, image);
    if (!described.has_value())
    {
        return refuse(an::ErrorCode::NotInitialized, "the image holds nothing to export");
    }
    an::Result<ExportPlan> plan =
        std::visit([&](const auto& typed) { return planExport(*described, typed.metadata(), request); }, image);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<SharedImageBuffer> buffer = allocateBuffer(plan->spec);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    if (const an::Status exported = exportImageInto(image, request, *buffer); exported.failed())
    {
        return exported;
    }
    return buffer;
}

an::Status SharedBufferManager::exportImageInto(const an::consumer::AnyImage& image, const ImageExportRequest& request,
                                                SharedImageBuffer& buffer) const
{
    return std::visit(
        [&](const auto& typed) -> an::Status {
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!described.has_value())
            {
                return refuse(an::ErrorCode::NotInitialized, "the image holds nothing to export");
            }
            an::Result<ExportPlan> plan = planExport(*described, typed.metadata(), request);
            if (!plan.hasValue())
            {
                return plan.status();
            }
            // Every lent buffer has a sidecar, stating at least what the buffer is: the host writes what an extension
            // sets on a result into the sidecar of the buffer it answers, so a buffer without one would lose it. A
            // buffer keeps the one it has.
            const std::filesystem::path sidecar = buffer.m_descriptor.metadata_sidecar.empty()
                                                      ? sidecarPathOf(buffer.m_filePath)
                                                      : std::filesystem::path(buffer.m_descriptor.metadata_sidecar);
            plan->spec.metadataSidecar = sidecar.lexically_normal().generic_string();
            if (const an::Status restated = restateBuffer(plan->spec, buffer); restated.failed())
            {
                return restated;
            }

            an::Result<an::RawFrame> frame = borrowMapping(buffer, true);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            if (const an::Status copied = copyRowsToFrame(typed, plan->region, *frame); copied.failed())
            {
                return copied;
            }
            frame->release();

            const an::ImageExtent& extent = described->extent();
            an::CodecMetadata codec = typed.metadata().codec();
            if (plan->cropped)
            {
                an::Result<an::CodecMetadata> moved = regionHeader(codec, extent, plan->region);
                if (!moved.hasValue())
                {
                    return moved.status();
                }
                codec = std::move(*moved);
            }
            const BufferStructure structure{.width = static_cast<std::uint32_t>(plan->region.width()),
                                            .height = static_cast<std::uint32_t>(plan->region.height()),
                                            .channels = extent.channelCount(),
                                            .pixelType = plan->spec.pixelType};
            // Rendered every time, written only when it differs from what the file holds: the cards of a part move as
            // the part does, a reduction's carry no revision of their own, and both are compared as bytes.
            QByteArray rendered = renderedSidecar(sidecarDocument(codec, structure, request.fileName));
            if (buffer.m_sidecarWrites == 0u || rendered != buffer.m_sidecarBytes)
            {
                if (const an::Status written = writeSidecar(rendered, sidecar); written.failed())
                {
                    return written;
                }
                buffer.m_sidecarBytes = std::move(rendered);
                ++buffer.m_sidecarWrites;
            }
            return an::Status::success();
        },
        image);
}

an::Status SharedBufferManager::restateBuffer(const SharedImageBufferSpec& spec, SharedImageBuffer& buffer) const
{
    if (!buffer.valid())
    {
        return refuse(an::ErrorCode::NotInitialized, "the buffer holds no mapping to restate");
    }
    const SharedBufferDescriptor& held = buffer.m_descriptor;
    if (spec.width != held.width || spec.height != held.height || spec.channels != held.channels ||
        spec.pixelType != held.pixel_type)
    {
        return refuse(an::ErrorCode::InvalidArgument,
                      "a buffer of " + std::to_string(held.width) + " x " + std::to_string(held.height) + " x " +
                          std::to_string(held.channels) + " " + protocolTypeName(held.pixel_type) + " cannot hold " +
                          std::to_string(spec.width) + " x " + std::to_string(spec.height) + " x " +
                          std::to_string(spec.channels) + " " + protocolTypeName(spec.pixelType));
    }
    SharedBufferDescriptor stated = held;
    stated.symbolic_name = spec.symbolicName;
    stated.pixel_format = spec.pixelFormat;
    stated.color_space = spec.colorSpace;
    stated.access_mode = spec.accessMode;
    stated.buffer_role = spec.bufferRole;
    stated.offset_x = spec.offsetX;
    stated.offset_y = spec.offsetY;
    stated.source_width = spec.sourceWidth;
    stated.source_height = spec.sourceHeight;
    stated.metadata_sidecar = spec.metadataSidecar;
    const auto validation = validateSharedBufferDescriptor(stated);
    if (!validation.ok())
    {
        return refuse(an::ErrorCode::InvalidArgument, validation.error);
    }
    buffer.m_descriptor = std::move(stated);
    return an::Status::success();
}

an::Status SharedBufferManager::clearBuffer(SharedImageBuffer& buffer) const
{
    if (!buffer.valid())
    {
        return refuse(an::ErrorCode::NotInitialized, "the buffer holds no mapping to clear");
    }
    std::memset(buffer.bytes(), 0, static_cast<std::size_t>(buffer.mappedSize()));
    return an::Status::success();
}

an::Result<SharedImageBuffer> SharedBufferManager::openBuffer(const SharedBufferDescriptor& descriptor) const
{
    const SharedBufferFileMapper mapper(m_rootPath);
    auto mapping = std::make_shared<SharedBufferFileMapping>();
    std::string error;
    if (!mapper.openBuffer(descriptor, mapping.get(), &error))
    {
        const bool external = error.starts_with("Failed to") || error.starts_with("Shared buffer file");
        return refuse(external ? an::ErrorCode::ExternalFailure : an::ErrorCode::InvalidArgument, std::move(error));
    }
    SharedImageBuffer buffer;
    buffer.m_descriptor = descriptor;
    buffer.m_filePath = mapping->file_path();
    buffer.m_mapping = std::move(mapping);
    return buffer;
}

an::Result<an::RawFrame> SharedBufferManager::borrowFrame(SharedImageBuffer& buffer)
{
    return borrowMapping(buffer, buffer.descriptor().access_mode == SharedBufferAccessMode::ReadWrite);
}

an::Result<ImportedImage> SharedBufferManager::importImage(const an::consumer::EngineSession& session,
                                                           const SharedBufferDescriptor& descriptor) const
{
    an::Result<SharedImageBuffer> buffer = openBuffer(descriptor);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    an::Result<ImportedImage> imported = importMapped(session, *buffer);
    buffer->release();
    return imported;
}

an::Result<ImportedImage> SharedBufferManager::importMapped(const an::consumer::EngineSession& session,
                                                            SharedImageBuffer& buffer) const
{
    if (!buffer.valid())
    {
        return refuse(an::ErrorCode::NotInitialized, "the buffer holds no mapping to import");
    }
    const SharedBufferDescriptor& descriptor = buffer.descriptor();
    an::Result<an::RawFrame> frame = borrowFrame(buffer);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const std::optional<an::RawFrameDescriptor> frameDescriptor = frame->descriptor();
    const std::optional<an::ImageDescriptor> imageDescriptor =
        frameDescriptor.has_value() ? frameDescriptor->toImageDescriptor() : std::nullopt;
    if (!imageDescriptor.has_value())
    {
        return refuse(an::ErrorCode::Unsupported, "the buffer's representation is not one the engine holds");
    }
    an::Result<SidecarContents> sidecar = readSidecar(m_rootPath, descriptor);
    if (!sidecar.hasValue())
    {
        return sidecar.status();
    }
    std::string fileName = sidecar->fileName;
    an::Result<an::ImageMetadata> metadata =
        importedMetadata(descriptor, imageDescriptor->extent(), std::move(*sidecar));
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }

    an::Result<an::consumer::AnyImage> image =
        refuse(an::ErrorCode::Internal, "the buffer's representation reached no copy");
    switch (descriptor.pixel_type)
    {
    case SharedBufferPixelType::UInt8:
        image = copyFrameToImage<std::uint8_t>(hold->engine(), *imageDescriptor, *frame, std::move(*metadata));
        break;
    case SharedBufferPixelType::UInt16:
        image = copyFrameToImage<std::uint16_t>(hold->engine(), *imageDescriptor, *frame, std::move(*metadata));
        break;
    case SharedBufferPixelType::Float32:
        image = copyFrameToImage<float>(hold->engine(), *imageDescriptor, *frame, std::move(*metadata));
        break;
    case SharedBufferPixelType::Float64:
        image = copyFrameToImage<double>(hold->engine(), *imageDescriptor, *frame, std::move(*metadata));
        break;
    }
    if (!image.hasValue())
    {
        return image.status();
    }
    frame->release();

    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move(*image));
    if (!asset.hasValue())
    {
        return asset.status();
    }
    ImportedImage imported;
    imported.asset = std::move(*asset);
    imported.name = descriptor.symbolic_name.empty() ? descriptor.buffer_id : descriptor.symbolic_name;
    imported.fileName = std::move(fileName);
    return imported;
}

an::Status SharedBufferManager::removeSessionBuffers(const std::string_view sessionId) const
{
    if (sessionId.empty())
    {
        return refuse(an::ErrorCode::InvalidArgument, "a shared buffer session has an identity");
    }
    if (m_rootPath.empty())
    {
        return refuse(an::ErrorCode::InvalidArgument, "the shared buffer root path is empty");
    }
    const std::filesystem::path session = m_rootPath / std::string(sessionId);
    std::error_code ec;
    if (!std::filesystem::exists(session, ec))
    {
        return an::Status::success();
    }
    std::filesystem::remove_all(session, ec);
    if (ec)
    {
        return refuse(an::ErrorCode::ExternalFailure,
                      "the shared buffer session directory cannot be removed: " + session.generic_string());
    }
    return an::Status::success();
}

an::Result<SharedBufferCleanupResult>
SharedBufferManager::cleanupStaleSessions(const std::vector<std::string>& liveSessionIds) const
{
    SharedBufferCleanupResult result;
    if (m_rootPath.empty())
    {
        return refuse(an::ErrorCode::InvalidArgument, "the shared buffer root path is empty");
    }
    std::error_code ec;
    if (!std::filesystem::exists(m_rootPath, ec))
    {
        return result;
    }
    std::set<std::string, std::less<>> live;
    for (const std::string& sessionId : liveSessionIds)
    {
        if (!sessionId.empty())
        {
            live.insert(sessionId);
        }
    }
    for (std::filesystem::directory_iterator it(m_rootPath, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_directory(ec))
        {
            continue;
        }
        if (live.contains(it->path().filename().string()))
        {
            continue;
        }
        int files = 0;
        for (std::filesystem::recursive_directory_iterator inner(it->path(), ec), innerEnd; inner != innerEnd && !ec;
             inner.increment(ec))
        {
            if (inner->is_regular_file(ec))
            {
                ++files;
            }
        }
        if (ec)
        {
            return refuse(an::ErrorCode::ExternalFailure,
                          "the shared buffer session directory cannot be read: " + it->path().generic_string());
        }
        std::filesystem::remove_all(it->path(), ec);
        if (ec)
        {
            return refuse(an::ErrorCode::ExternalFailure,
                          "the shared buffer session directory cannot be removed: " + it->path().generic_string());
        }
        result.removedSessionCount += 1;
        result.removedBufferCount += files;
    }
    if (ec)
    {
        return refuse(an::ErrorCode::ExternalFailure,
                      "the shared buffer root cannot be enumerated: " + m_rootPath.generic_string());
    }
    return result;
}

} // namespace an::ui::extensions
