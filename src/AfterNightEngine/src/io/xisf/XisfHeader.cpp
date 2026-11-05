// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - describing an XISF image and reading its cards.
//
// The description is taken from the image's declarations without its pixel data: the channel meaning
// from the colour space and channel count, the numeric domain from the declared sample range, and a
// typed mosaic only from a filter array this engine names. Every declaration is also preserved as a
// card, so one the engine cannot type still survives a round trip; a property longer than one card
// continues in the cards after it.

#include "io/xisf/XisfFile.h"

#include <AfterNightEngine/IO/CodecMetadataPolicy.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "image/CardContinuation.h"

#include <openxisf/property.h>

#include <string>
#include <utility>

namespace an
{
namespace
{
/// What the channels are and in which basis, for a colour space and a channel count.
///
/// A count the colour space does not define is described rather than guessed: `Named` says the
/// meaning of the channels is carried outside this vocabulary, which is exactly true of an XISF
/// image with alpha channels or with more bands than its colour space names.
std::pair<ChannelSchema, ColorSpace> xisfChannels(openxisf::color_space space, std::uint64_t channels) noexcept
{
    if (space == openxisf::color_space::gray && channels == 1u)
        return {ChannelSchema::Mono, ColorSpace::Unspecified};
    if (space == openxisf::color_space::rgb && channels == 3u)
        return {ChannelSchema::Rgb, ColorSpace::Unspecified};
    if (space == openxisf::color_space::rgb && channels == 4u)
        return {ChannelSchema::Rgba, ColorSpace::Unspecified};
    if (space == openxisf::color_space::cie_lab && channels == 3u)
        return {ChannelSchema::Lab, ColorSpace::CieLab};
    return {ChannelSchema::Named, ColorSpace::Unspecified};
}

/// The filter array a two-by-two or six-by-six XISF pattern of red, green and blue filters is.
///
/// A pattern this engine does not name - a four-colour array, a block of another size, a filter
/// letter outside RGB - yields nothing. The declaration is still preserved as a card, so the file's
/// own statement survives a round trip; what it does not do is become a typed mosaic that a
/// demosaic would then be run against.
std::optional<CfaDescriptor> xisfFilterArray(const openxisf::color_filter_array& filters) noexcept
{
    const auto letters = std::string_view(filters.pattern);
    if (filters.width != filters.height || letters.size() != filters.width * filters.height)
        return std::nullopt;
    const auto color = [](char letter) noexcept -> std::optional<CfaColor> {
        if (letter == 'R')
            return CfaColor::Red;
        if (letter == 'G')
            return CfaColor::Green;
        if (letter == 'B')
            return CfaColor::Blue;
        return std::nullopt;
    };
    if (filters.width == 2u)
    {
        if (letters == "RGGB")
            return CfaDescriptor::create(CfaPattern::Rggb, 0u, 0u);
        if (letters == "BGGR")
            return CfaDescriptor::create(CfaPattern::Bggr, 0u, 0u);
        if (letters == "GRBG")
            return CfaDescriptor::create(CfaPattern::Grbg, 0u, 0u);
        if (letters == "GBRG")
            return CfaDescriptor::create(CfaPattern::Gbrg, 0u, 0u);
        return std::nullopt;
    }
    if (filters.width != kCfaTableSide)
        return std::nullopt;
    CfaColorEntries entries{};
    for (std::size_t index = 0u; index < letters.size(); ++index)
    {
        const auto entry = color(letters[index]);
        if (!entry)
            return std::nullopt;
        entries[index] = *entry;
    }
    const auto table = CfaFilterTable::create(entries);
    if (!table)
        return std::nullopt;
    return CfaDescriptor::create(entries, 0u, 0u);
}

} // namespace

Result<std::vector<CodecCard>> readXisfCards(const openxisf::reader& reader, const openxisf::image_info& image,
                                             const ExecutionContext& execution, std::string_view path)
{
    std::vector<CodecCard> cards;
    const auto& fits = image.fits_keywords;
    const auto& properties = image.properties;
    const auto& unit = reader.metadata();
    // The structural cards precede them: the facts the file states as attributes and elements of the image
    // rather than as properties, which nothing else preserves and an encoder decides afresh. Each is written only
    // when the file states it, but for the colour space, which every image has.
    std::vector<std::pair<std::string_view, std::string>> structural;
    structural.emplace_back(kXisfColorSpaceKeyword, std::string(openxisf::color_space_name(image.color_space)));
    if (image.image_type)
        structural.emplace_back(kXisfImageTypeKeyword, std::string(openxisf::image_type_name(*image.image_type)));
    if (image.bounds)
        structural.emplace_back(kXisfBoundsKeyword,
                                xisfNumberText(image.bounds->lower) + ":" + xisfNumberText(image.bounds->upper));
    if (image.color_filter_array)
        structural.emplace_back(kXisfFilterArrayKeyword, xisfFilterArrayText(*image.color_filter_array));
    if (image.offset != 0.0)
        structural.emplace_back(kXisfOffsetKeyword, xisfNumberText(image.offset));
    if (image.orientation != openxisf::orientation::none)
        structural.emplace_back(kXisfOrientationKeyword, std::string(openxisf::orientation_name(image.orientation)));
    if (!image.id.empty())
        structural.emplace_back(kXisfIdKeyword, image.id);
    if (!image.uuid.empty())
        structural.emplace_back(kXisfUuidKeyword, image.uuid);
    if (image.rgb_working_space)
        structural.emplace_back(kXisfWorkingSpaceKeyword, xisfWorkingSpaceText(*image.rgb_working_space));
    if (image.display_function)
        structural.emplace_back(kXisfDisplayFunctionKeyword, xisfDisplayFunctionText(*image.display_function));
    if (image.resolution)
        structural.emplace_back(kXisfResolutionKeyword, xisfResolutionText(*image.resolution));
    const auto total = structural.size() + fits.size() + properties.size() + unit.size();
    if (total > kMaxCodecCards)
        return Status::failure(ErrorCode::LimitExceeded,
                               {.message = "XISF image states more cards than one image preserves",
                                .operation = "read XISF cards",
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofCount(ContextKey::Requested, total),
                                            StatusContextField::ofCount(ContextKey::Limit, kMaxCodecCards)}});
    cards.reserve(total);
    const auto append = [&](CodecNamespace origin, std::string keyword, std::string value,
                            std::string comment) -> Status {
        auto card = CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
        if (!card.hasValue())
            return card.status();
        cards.push_back(std::move(*card));
        return {};
    };
    for (auto& [keyword, value] : structural)
        if (const auto added = append(CodecNamespace::Xisf, std::string(keyword), std::move(value),
                                      std::string(keyword) + " the file declares");
            added.failed())
            return added;
    for (const auto& keyword : fits)
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (const auto added = append(CodecNamespace::Fits, keyword.name, keyword.value, keyword.comment);
            added.failed())
            return added;
    }
    // The image's own properties, then the unit's metadata, which says who wrote the file and how; an encoder
    // states its own and never writes these back. Each card's comment states the property's type, and a value
    // longer than one card - a matrix of an astrometric solution, a long processing history - continues in the cards
    // after it. What the image's cards cannot hold is refused by name rather than truncated, the text of a value
    // that cannot fit never built; a caller that wants the pixels of such a file asks for them without metadata.
    for (const auto* list : {&properties, &unit})
        for (const auto& property : *list)
        {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto refused = [&] {
                return Status::failure(
                    ErrorCode::LimitExceeded,
                    {.message = "an XISF image states more metadata than one image preserves; read this image "
                                "without metadata to obtain its samples",
                     .operation = "read XISF property",
                     .domain = ErrorDomain::Io,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, property.id),
                                 StatusContextField::ofText(ContextKey::Path, std::string(path)),
                                 StatusContextField::ofCount(ContextKey::Limit, kMaxCodecCards)}});
            };
            const std::size_t left = cards.size() < kMaxCodecCards ? kMaxCodecCards - cards.size() : 0u;
            const auto text = xisfPropertyText(property.value, left * kMaxCodecCardTextBytes);
            const auto comment = xisfPropertyComment(property);
            if (!text || comment.size() > kMaxCodecCardTextBytes)
                return refused();
            if (const auto added =
                    appendFieldCards(cards, CodecNamespace::Xisf, property.id, *text, comment, "read XISF property");
                added.failed())
                return added.code() == ErrorCode::LimitExceeded ? refused() : added;
        }
    return cards;
}

Result<XisfHeader> readXisfHeader(const openxisf::reader& reader, const EngineContext& engine,
                                  const ExecutionContext& execution, const CodecReadRequest& request)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    auto described = evaluateAcrossBoundary(kXisfBoundary, [&]() -> Result<XisfHeader> {
        try
        {
            const auto images = reader.images();
            if (images.empty())
            {
                // An image whose own declarations cannot be used - a geometry with no pixels, a block of another
                // size than its geometry gives - is left out of the unit, with an error the person is told.
                std::string message = "XISF unit holds no readable image";
                for (const auto& diagnostic : reader.diagnostics())
                    if (diagnostic.severity == openxisf::severity::error)
                    {
                        message += ": " + diagnostic.message;
                        break;
                    }
                return xisfFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "describe XISF image",
                                   request.path, message);
            }
            if (request.imageIndex >= images.size())
                return Status::failure(
                    ErrorCode::OutOfRange,
                    {.message = "the requested image index is outside the XISF unit",
                     .operation = "read XISF header",
                     .domain = ErrorDomain::Io,
                     .context = {StatusContextField::ofText(ContextKey::Parameter, "imageIndex"),
                                 StatusContextField::ofCount(ContextKey::Requested, request.imageIndex),
                                 StatusContextField::ofCount(ContextKey::Available, images.size())}});
            const openxisf::image_info& image = images[request.imageIndex];
            const auto format = xisfSampleFormat(image.sample_format);
            const auto& dimensions = image.geometry.dimensions;
            // An image of more than two axes - a cube - has no description in this engine, and a one-axis image is
            // a single row.
            if (!format || dimensions.empty() || dimensions.size() > 2u)
                return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Xisf,
                                    "describe XISF image", request.path);
            const auto width = dimensions[0];
            const auto height = dimensions.size() == 2u ? dimensions[1] : std::uint64_t{1u};
            const auto limits = engine.limits();
            if (width > limits.maxImageWidth || height > limits.maxImageHeight ||
                image.geometry.channels > limits.maxChannelCount)
                return Status::failure(ErrorCode::LimitExceeded);
            const auto channels = static_cast<std::uint32_t>(image.geometry.channels);
            const auto extent = ImageExtent::create(static_cast<std::uint32_t>(width),
                                                    static_cast<std::uint32_t>(height), channels, limits);
            if (!extent)
                return Status::failure(ErrorCode::LimitExceeded);
            const auto [schema, space] = xisfChannels(image.color_space, image.geometry.channels);
            const auto cfa = !image.color_filter_array || channels != 1u || schema != ChannelSchema::Mono
                                 ? std::optional<CfaDescriptor>{}
                                 : xisfFilterArray(*image.color_filter_array);
            // XISF normalizes every representation over the range its image declares, and the default
            // range is the normalized one. A floating image that declares another range holds values in
            // that range and nothing here rescales them, so the domain says so rather than claiming a
            // fraction of full scale the samples do not carry.
            const bool normalized = !image.bounds || (image.bounds->lower == 0.0 && image.bounds->upper == 1.0);
            const bool physical = !normalized && format->sampleClass == RawSampleClass::FloatingPoint;
            const auto domain = space == ColorSpace::CieLab ? NumericDomain::EncodedNonlinear
                                : physical                  ? NumericDomain::PhysicalScaled
                                                            : NumericDomain::NormalizedLinear;
            const auto descriptor = RawFrameDescriptor::create({.extent = *extent,
                                                                .sampleFormat = *format,
                                                                .byteOrder = ByteOrder::Native,
                                                                .channelSchema = schema,
                                                                .colorSpace = space,
                                                                .numericDomain = domain,
                                                                .cfa = cfa.value_or(CfaDescriptor{}),
                                                                .coordinateOrigin = CoordinateOrigin::TopLeft,
                                                                .pixelAspectRatio = 1.0});
            if (!descriptor)
                return Status::failure(ErrorCode::InvalidData);
            const bool planar = image.pixel_storage == openxisf::pixel_storage::planar;
            const auto layout = ImageLayout::create({.extent = *extent,
                                                     .elementSizeBytes = format->sizeBytes,
                                                     .samplesPerPixel = planar ? 1u : channels,
                                                     .planeCount = planar ? channels : 1u,
                                                     .rowAlignmentBytes = 1u},
                                                    limits);
            if (!layout)
                return Status::failure(ErrorCode::LimitExceeded);
            // The block's facts are asked for once, here. A block the library could not use leaves the image
            // described - its header was read - and makes its decode refuse with the block's own reason.
            std::optional<openxisf::block_info> block;
            Status blockFailure;
            try
            {
                block = reader.block(request.imageIndex);
            }
            catch (const openxisf::error& error)
            {
                blockFailure = xisfLibraryFailure(error, XisfStage::Read, "decode XISF pixels", request.path);
            }
            std::vector<CodecCard> cards;
            if (request.readMetadata)
            {
                auto read = readXisfCards(reader, image, execution, request.path);
                if (!read.hasValue())
                    return read.status();
                cards = std::move(*read);
            }
            // The source encoding states the file's own representation. XISF applies no scale or
            // offset to reach the values the image holds, so neither is claimed here.
            auto codec =
                CodecMetadata::create({*format, ByteOrder::Native, std::nullopt, std::nullopt}, std::move(cards));
            if (!codec.hasValue())
                return codec.status();
            ImageMetadata metadata;
            if (request.readMetadata)
            {
                auto projected = xisfMetadata(std::move(*codec), *extent, request.path, image.properties);
                if (!projected.hasValue())
                    return projected.status();
                metadata = std::move(*projected);
            }
            else
                metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
            return XisfHeader{.info = {*descriptor, *layout, static_cast<std::uint64_t>(images.size()),
                                       std::move(metadata), CodecProbeDisposition::DecodeRequired},
                              .storage = image.pixel_storage,
                              .sampleFormat = image.sample_format,
                              .axes = static_cast<std::uint32_t>(dimensions.size()),
                              .block = block,
                              .blockFailure = std::move(blockFailure)};
        }
        catch (const openxisf::error& error)
        {
            return xisfLibraryFailure(error, XisfStage::Open, "describe XISF image", request.path);
        }
    });
    if (!described.hasValue() || !described->hasValue())
    {
        const auto& status = described.hasValue() ? described->status() : described.status();
        if (codecFailureReason(status).has_value() || status.code() == ErrorCode::OutOfRange ||
            status.code() == ErrorCode::LimitExceeded || status.code() == ErrorCode::Cancelled ||
            status.code() == ErrorCode::DeadlineExceeded || status.code() == ErrorCode::AllocationFailed)
            return status;
        return codecFailure(status.code(), CodecFailure::MetadataUnreadable, CodecFormat::Xisf, "describe XISF image",
                            request.path, status);
    }
    return std::move(**described);
}
} // namespace an
