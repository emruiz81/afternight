// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening a PNG container, describing it, reading its chunks into cards, and
// decoding its pixels.
//
// The header read applies the expansions decoding this container is - a palette to the colours its
// table gives, a sub-byte grey to eight bits, any transparency key to an alpha channel - and puts
// sixteen-bit samples in the host's order, recording the file's big-endian order as a source fact.
// The reader is bounded by the engine context's largest width and height before the header is
// read. The colour declaration decides the description: an embedded profile means encoded samples
// of no stated encoding, the standard colour space means the sRGB transfer, a gamma of one means
// linear samples, any other gamma a pure power law of its reciprocal, and no declaration means
// encoded samples of no stated encoding. Every pass of an interlaced payload is read into one zeroed
// frame, and a text entry that packs an Exif block as hexadecimal is read as that block.

#include "io/png/PngFile.h"

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "interop/exiv2/ExifCards.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>

namespace an
{
namespace
{

/// What a text entry packing an Exif block states before the block itself.
constexpr std::string_view kExifTextPrefix{"Exif\0\0", 6u};

/// Keeps what the library said about a refusal, then leaves the frame it was called from.
///
/// @param png the library's reader.
/// @param text what it said.
void pngReadError(png_structp png, png_const_charp text)
{
    auto* const session = static_cast<PngReadSession*>(png_get_error_ptr(png));
    if (session != nullptr && text != nullptr)
    {
        const auto length = (std::min)(std::strlen(text), kPngMessageBytes - 1u);
        std::memcpy(session->message, text, length);
        session->message[length] = '\0';
    }
    png_longjmp(png, 1);
}

/// Swallows what the library would otherwise write to the process's error stream.
void pngReadWarning(png_structp, png_const_charp) {}

/// Pulls the next bytes of the container out of the stream.
///
/// A stream that has fewer bytes than the container claims ends the read where it actually ends:
/// the flag turns the result into a refusal rather than an image whose last rows are invented.
///
/// @param png the library's reader.
/// @param data where the bytes go.
/// @param size how many are wanted.
void pngReadBytes(png_structp png, png_bytep data, png_size_t size)
{
    auto* const session = static_cast<PngReadSession*>(png_get_io_ptr(png));
    session->stream.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (static_cast<std::size_t>(session->stream.gcount()) != size)
    {
        session->truncated = true;
        png_error(png, "the payload ends before the container says it does");
    }
}

/// What runs between two decoded rows.
struct RowProgress
{
    const ExecutionContext* execution = nullptr; ///< Whose cancellation is checked.
    std::uint64_t total = 0u; ///< How many rows every pass together holds.
};

/// Checks cancellation and reports progress between rows.
///
/// @param user the progress record.
/// @param rows how many rows are decoded.
/// @return false when the caller has asked the operation to stop.
bool pngRowStep(void* user, std::uint64_t rows)
{
    auto* const progress = static_cast<RowProgress*>(user);
    if (progress->execution->checkCancellation().failed())
    {
        return false;
    }
    progress->execution->reportProgress(rows, progress->total, "decode PNG");
    return true;
}

/// Which transfer function a container's declaration names.
///
/// @param declaration what the container declares.
/// @return the transfer function it states, or `TransferFunction::Unspecified` when it states none.
[[nodiscard]] TransferFunction pngTransferOf(const PngColorDeclaration& declaration) noexcept
{
    // An embedded profile decides the encoding, and this engine does not interpret one - so what it
    // states is that the samples are encoded, not which encoding they are in.
    if (declaration.profile)
    {
        return TransferFunction::Unspecified;
    }
    if (declaration.standard)
    {
        return TransferFunction::Srgb;
    }
    if (declaration.gamma)
    {
        return declaration.fileGamma > 0.99999 && declaration.fileGamma < 1.00001 ? TransferFunction::Linear
                                                                                  : TransferFunction::PureGamma;
    }
    return TransferFunction::Unspecified;
}

/// The name a colour type goes by, for the card that preserves it.
///
/// @param colorType the colour type the file states.
/// @return its name.
[[nodiscard]] std::string pngColorTypeName(int colorType)
{
    switch (colorType)
    {
    case PNG_COLOR_TYPE_GRAY:
        return "Gray";
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        return "GrayAlpha";
    case PNG_COLOR_TYPE_PALETTE:
        return "Palette";
    case PNG_COLOR_TYPE_RGB:
        return "Rgb";
    case PNG_COLOR_TYPE_RGB_ALPHA:
        return "RgbAlpha";
    default:
        break;
    }
    return "Unknown";
}

/// Reads back the bytes a writer packed into a text entry as hexadecimal.
///
/// The packing states a blank line, the profile's name, its length and then the bytes, and it is
/// what a well-known image tool writes instead of the container's own Exif chunk.
///
/// @param text the entry's value.
/// @return the bytes, which are empty when the entry does not hold any.
[[nodiscard]] std::vector<std::byte> parseRawProfile(std::string_view text)
{
    std::size_t offset = 0u;
    for (int line = 0; line < 3 && offset != std::string_view::npos; ++line)
    {
        offset = text.find('\n', offset);
        if (offset != std::string_view::npos)
        {
            ++offset;
        }
    }
    std::vector<std::byte> bytes;
    if (offset == std::string_view::npos)
    {
        return bytes;
    }
    unsigned value = 0u;
    int digits = 0;
    for (; offset < text.size(); ++offset)
    {
        const char character = text[offset];
        unsigned digit = 0u;
        if (character >= '0' && character <= '9')
        {
            digit = static_cast<unsigned>(character - '0');
        }
        else if (character >= 'a' && character <= 'f')
        {
            digit = static_cast<unsigned>(character - 'a') + 10u;
        }
        else if (character >= 'A' && character <= 'F')
        {
            digit = static_cast<unsigned>(character - 'A') + 10u;
        }
        else
        {
            continue;
        }
        value = (value << 4u) | digit;
        if (++digits == 2)
        {
            bytes.push_back(static_cast<std::byte>(value));
            value = 0u;
            digits = 0;
        }
    }
    return bytes;
}

/// Appends the cards an Exif block states, whichever chunk carried it.
///
/// @param cards where to append.
/// @param block the block, with or without the prefix a marker states before one.
/// @return success, or a typed metadata or limit failure.
[[nodiscard]] Status appendExifCards(std::vector<CodecCard>& cards, std::span<const std::byte> block)
{
    const std::string_view text{reinterpret_cast<const char*>(block.data()), block.size()};
    if (text.starts_with(kExifTextPrefix))
    {
        block = block.subspan(kExifTextPrefix.size());
    }
    auto exif = readExifBlock(CodecNamespace::Png, block);
    if (!exif.hasValue())
    {
        return exif.status();
    }
    for (CodecCard& card : *exif)
    {
        if (cards.size() >= kMaxCodecCards)
        {
            return Status::failure(ErrorCode::LimitExceeded);
        }
        cards.push_back(std::move(card));
    }
    return {};
}

/// Appends every text entry one information block states.
///
/// @param cards where to append.
/// @param session the open session.
/// @param info which block to read.
/// @return success, or a typed metadata or limit failure.
[[nodiscard]] Status appendTextCards(std::vector<CodecCard>& cards, const PngReadSession& session, png_infop info)
{
    png_textp entries = nullptr;
    int count = 0;
    if (png_get_text(session.png, info, &entries, &count) == 0u || entries == nullptr)
    {
        return {};
    }
    for (int index = 0; index < count; ++index)
    {
        const png_text& entry = entries[index];
        if (entry.key == nullptr)
        {
            continue;
        }
        const std::string keyword(entry.key);
        std::size_t length = entry.text_length != 0u ? entry.text_length : entry.itxt_length;
        if (length == 0u && entry.text != nullptr)
        {
            length = std::strlen(entry.text);
        }
        const std::string text(entry.text != nullptr ? entry.text : "", length);
        if (keyword == kPngRawExifKeyword || keyword == kPngRawApp1Keyword)
        {
            const auto block = parseRawProfile(text);
            if (!block.empty())
            {
                if (const auto added = appendExifCards(cards, block); added.failed())
                {
                    return added;
                }
            }
            continue;
        }
        const char* const kind = entry.compression == PNG_TEXT_COMPRESSION_zTXt   ? "zTXt"
                                 : entry.compression == PNG_TEXT_COMPRESSION_NONE ? "tEXt"
                                                                                  : "iTXt";
        if (const auto added = appendContinuedCard(cards, CodecNamespace::Png, keyword, text, kind); added.failed())
        {
            return added;
        }
    }
    return {};
}

} // namespace

void PngReadSessionDeleter::operator()(PngReadSession* session) const noexcept
{
    if (session == nullptr)
    {
        return;
    }
    if (session->png != nullptr)
    {
        png_destroy_read_struct(&session->png, session->info != nullptr ? &session->info : nullptr,
                                session->endInfo != nullptr ? &session->endInfo : nullptr);
    }
    delete session;
}

Status pngFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                  std::string_view message)
{
    return codecFailure(
        code, reason, CodecFormat::Png, operation, path,
        Status::failure(
            code, {.message = std::string(message), .operation = std::string(operation), .domain = ErrorDomain::Io}));
}

bool pngReadHeader(PngReadSession* session, std::uint32_t maxWidth, std::uint32_t maxHeight) noexcept
{
    if (setjmp(png_jmpbuf(session->png)) != 0)
    {
        return false;
    }
    png_set_user_limits(session->png, PNG_UINT_31_MAX, PNG_UINT_31_MAX);
    png_set_read_fn(session->png, session, &pngReadBytes);
    png_read_info(session->png, session->info);
    session->width = png_get_image_width(session->png, session->info);
    session->height = png_get_image_height(session->png, session->info);
    if (session->width > maxWidth || session->height > maxHeight)
        return false;
    session->sourceBitDepth = png_get_bit_depth(session->png, session->info);
    session->sourceColorType = png_get_color_type(session->png, session->info);
    // Two expansions, and both of them are what decoding this container is rather than a conversion
    // of what it holds: a palette index stands for the colour the file's own table gives it, and a
    // sub-byte grey states the same fraction of full scale that eight bits of it do.
    if (session->sourceColorType == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(session->png);
    }
    else if (session->sourceColorType == PNG_COLOR_TYPE_GRAY && session->sourceBitDepth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(session->png);
    }
    // Transparency keys also belong to greyscale and true-colour images, at every source width.
    if (png_get_valid(session->png, session->info, PNG_INFO_tRNS) != 0u)
    {
        png_set_tRNS_to_alpha(session->png);
    }
    if (session->sourceBitDepth == 16 && std::endian::native == std::endian::little)
    {
        png_set_swap(session->png);
    }
    session->passes = png_set_interlace_handling(session->png);
    png_read_update_info(session->png, session->info);
    session->width = png_get_image_width(session->png, session->info);
    session->height = png_get_image_height(session->png, session->info);
    session->bitDepth = png_get_bit_depth(session->png, session->info);
    session->colorType = png_get_color_type(session->png, session->info);
    session->channels = png_get_channels(session->png, session->info);
    return true;
}

bool pngDecodeRows(PngReadSession* session, const PngDecodeTarget* target) noexcept
{
    if (setjmp(png_jmpbuf(session->png)) != 0)
    {
        return false;
    }
    std::uint64_t completed = 0u;
    for (int pass = 0; pass < session->passes; ++pass)
    {
        for (std::uint32_t y = 0u; y < session->height; ++y)
        {
            png_read_row(session->png, reinterpret_cast<png_bytep>(target->bytes + y * target->rowStrideBytes),
                         nullptr);
            ++completed;
            if (target->step != nullptr && !target->step(target->user, completed))
            {
                session->stopped = true;
                return false;
            }
        }
    }
    png_read_end(session->png, session->endInfo);
    return true;
}

Result<PngReadHandle> openPng(const EngineContext& engine, std::string_view path)
{
    auto opened = evaluateAcrossBoundary(kPngBoundary, [&]() -> PngReadHandle {
        PngReadHandle session{new PngReadSession()};
        session->stream.open(
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())),
            std::ios::binary);
        return session;
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Png, "open PNG",
                            path, opened.status());
    }
    PngReadHandle session = std::move(*opened);
    if (!session->stream.is_open())
    {
        return pngFailure(ErrorCode::ExternalFailure, CodecFailure::MetadataUnreadable, "open PNG", path,
                          "the file could not be opened for reading");
    }
    session->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, session.get(), &pngReadError, &pngReadWarning);
    if (session->png == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    session->info = png_create_info_struct(session->png);
    session->endInfo = png_create_info_struct(session->png);
    if (session->info == nullptr || session->endInfo == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    const auto limits = engine.limits();
    if (!pngReadHeader(session.get(), limits.maxImageWidth, limits.maxImageHeight))
    {
        if (session->width > limits.maxImageWidth || session->height > limits.maxImageHeight)
        {
            const bool width = session->width > limits.maxImageWidth;
            return Status::failure(
                ErrorCode::LimitExceeded,
                {.message = "the PNG dimensions exceed the configured image limits",
                 .operation = "read PNG header",
                 .domain = ErrorDomain::Io,
                 .context = {
                     StatusContextField::ofText(ContextKey::Path, std::string(path)),
                     StatusContextField::ofText(ContextKey::Parameter, width ? "width" : "height"),
                     StatusContextField::ofCount(ContextKey::Requested, width ? session->width : session->height),
                     StatusContextField::ofCount(ContextKey::Limit,
                                                 width ? limits.maxImageWidth : limits.maxImageHeight)}});
        }
        return pngFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "read PNG header", path,
                          session->message);
    }
    return session;
}

PngColorDeclaration pngColorDeclaration(const PngReadSession& session) noexcept
{
    PngColorDeclaration declaration;
    declaration.profile = png_get_valid(session.png, session.info, PNG_INFO_iCCP) != 0u;
    int intent = 0;
    declaration.standard = png_get_sRGB(session.png, session.info, &intent) != 0u;
    png_fixed_point gamma = 0;
    if (png_get_gAMA_fixed(session.png, session.info, &gamma) != 0u && gamma > 0)
    {
        declaration.gamma = true;
        declaration.fileGamma = static_cast<double>(gamma) / 100000.0;
    }
    return declaration;
}

std::tuple<ColorSpace, NumericDomain, TransferFunction> pngColorMeaning(const PngColorDeclaration& declaration,
                                                                        std::uint32_t channels) noexcept
{
    const auto transfer = pngTransferOf(declaration);
    const bool colour = channels >= 3u;
    if (transfer == TransferFunction::Linear)
    {
        return {colour ? ColorSpace::LinearRgb : ColorSpace::Unspecified, NumericDomain::NormalizedLinear, transfer};
    }
    if (transfer == TransferFunction::Srgb || transfer == TransferFunction::PureGamma)
    {
        return {colour ? ColorSpace::DisplayRgb : ColorSpace::Unspecified, NumericDomain::EncodedNonlinear, transfer};
    }
    // A container that declares nothing about its colour is one whose samples are encoded - which
    // is what every one of this format's readers assumes - without stating what the encoding is.
    return {ColorSpace::Unspecified, NumericDomain::EncodedNonlinear, transfer};
}

Result<std::vector<CodecCard>> pngCards(const PngReadSession& session, bool includeEnd)
{
    std::vector<CodecCard> cards;
    if (const auto added =
            appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngColorTypeKeyword),
                                pngColorTypeName(session.sourceColorType), "colour type the file states");
        added.failed())
    {
        return added;
    }
    if (session.sourceBitDepth != session.bitDepth)
    {
        if (const auto added = appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngBitDepthKeyword),
                                                   std::to_string(session.sourceBitDepth),
                                                   "bits per sample the file stores, before the expansion decoding it "
                                                   "is");
            added.failed())
        {
            return added;
        }
    }
    const auto declaration = pngColorDeclaration(session);
    if (declaration.standard)
    {
        int intent = 0;
        static_cast<void>(png_get_sRGB(session.png, session.info, &intent));
        if (const auto added = appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngSrgbKeyword),
                                                   std::to_string(intent), "rendering intent the file declares");
            added.failed())
        {
            return added;
        }
    }
    if (declaration.gamma)
    {
        png_fixed_point gamma = 0;
        static_cast<void>(png_get_gAMA_fixed(session.png, session.info, &gamma));
        if (const auto added =
                appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngGammaKeyword), std::to_string(gamma),
                                    "image gamma the file declares, in hundred-thousandths");
            added.failed())
        {
            return added;
        }
    }
    if (declaration.profile)
    {
        png_charp name = nullptr;
        int compression = 0;
        png_bytep profile = nullptr;
        png_uint_32 profileBytes = 0u;
        if (png_get_iCCP(session.png, session.info, &name, &compression, &profile, &profileBytes) != 0u)
        {
            if (const auto added = appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngIccKeyword),
                                                       std::to_string(profileBytes),
                                                       std::string("bytes of embedded colour profile '") +
                                                           (name != nullptr ? name : "") + "', preserved as a fact");
                added.failed())
            {
                return added;
            }
        }
    }
    png_color_8p significant = nullptr;
    if (png_get_sBIT(session.png, session.info, &significant) != 0u && significant != nullptr)
    {
        const std::string text =
            session.sourceColorType == PNG_COLOR_TYPE_GRAY || session.sourceColorType == PNG_COLOR_TYPE_GRAY_ALPHA
                ? std::to_string(static_cast<int>(significant->gray))
                : std::to_string(static_cast<int>(significant->red)) + " " +
                      std::to_string(static_cast<int>(significant->green)) + " " +
                      std::to_string(static_cast<int>(significant->blue));
        if (const auto added = appendContinuedCard(cards, CodecNamespace::Png, std::string(kPngSignificantBitsKeyword),
                                                   text, "significant bits per channel the file declares");
            added.failed())
        {
            return added;
        }
    }
    for (png_infop info : {session.info, session.endInfo})
    {
        if (info == session.endInfo && !includeEnd)
        {
            continue;
        }
        if (const auto added = appendTextCards(cards, session, info); added.failed())
        {
            return added;
        }
        png_uint_32 exifBytes = 0u;
        png_bytep exif = nullptr;
        if (png_get_eXIf_1(session.png, info, &exifBytes, &exif) != 0u && exif != nullptr && exifBytes > 0u)
        {
            if (const auto added = appendExifCards(
                    cards, std::span<const std::byte>{reinterpret_cast<const std::byte*>(exif), exifBytes});
                added.failed())
            {
                return added;
            }
        }
    }
    return cards;
}

Result<ImageMetadata> pngMetadata(std::vector<CodecCard> cards, const PngColorDeclaration& declaration,
                                  const RawSampleFormat& sampleFormat, std::string_view path)
{
    auto codec = CodecMetadata::create(
        SourceEncoding{.sampleFormat = sampleFormat,
                       .byteOrder = sampleFormat.sizeBytes > 1u ? ByteOrder::Big : ByteOrder::Native},
        std::move(cards));
    if (!codec.hasValue())
    {
        return codec.status();
    }
    const auto projection = projectAcquisition(*codec);
    auto acquisition = AcquisitionMetadata::create(projection.acquisition);
    if (!acquisition.hasValue())
    {
        return acquisition.status();
    }
    const auto transfer = pngTransferOf(declaration);
    auto semantics = ImageSemantics::create({.transferFunction = transfer,
                                             .gammaExponent = transfer == TransferFunction::PureGamma
                                                                  ? std::optional<double>{1.0 / declaration.fileGamma}
                                                                  : std::nullopt});
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    auto provenance = Provenance::create(
        ProvenanceSourceKind::File,
        path.size() <= kMaxProvenanceTextBytes ? std::string(path) : "PNG source path exceeds lineage text bound");
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    return ImageMetadata::create(std::move(*acquisition), std::move(*semantics), {}, std::move(*provenance),
                                 std::move(*codec), std::nullopt);
}

Result<CodecImageInfo> describePng(PngReadSession& session, const EngineContext& engine,
                                   const CodecReadRequest& request)
{
    const auto channels = static_cast<std::uint32_t>(session.channels);
    if ((session.bitDepth != 8 && session.bitDepth != 16) || channels == 0u || channels > 4u)
    {
        return pngFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe PNG image",
                          request.path, "this container states a representation this engine does not describe");
    }
    const auto limits = engine.limits();
    const auto extent = ImageExtent::create(session.width, session.height, channels, limits);
    if (!extent)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    const RawSampleFormat format{static_cast<std::uint32_t>(session.bitDepth / 8), RawSampleClass::UnsignedInteger};
    const auto declaration = pngColorDeclaration(session);
    const auto [space, domain, transfer] = pngColorMeaning(declaration, channels);
    static_cast<void>(transfer);
    const auto schema = channels == 1u   ? ChannelSchema::Mono
                        : channels == 2u ? ChannelSchema::Named
                        : channels == 3u ? ChannelSchema::Rgb
                                         : ChannelSchema::Rgba;
    const auto descriptor = RawFrameDescriptor::create({.extent = *extent,
                                                        .sampleFormat = format,
                                                        .byteOrder = ByteOrder::Native,
                                                        .channelSchema = schema,
                                                        .colorSpace = space,
                                                        .numericDomain = domain,
                                                        .cfa = {},
                                                        .coordinateOrigin = CoordinateOrigin::TopLeft,
                                                        .pixelAspectRatio = 1.0});
    if (!descriptor)
    {
        return Status::failure(ErrorCode::InvalidData);
    }
    const auto layout = ImageLayout::create({.extent = *extent,
                                             .elementSizeBytes = format.sizeBytes,
                                             .samplesPerPixel = channels,
                                             .planeCount = 1u,
                                             .rowAlignmentBytes = 1u},
                                            limits);
    if (!layout)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    ImageMetadata metadata;
    if (request.readMetadata)
    {
        auto cards = pngCards(session, false);
        if (!cards.hasValue())
        {
            return cards.status();
        }
        auto projected = pngMetadata(std::move(*cards), declaration, format, request.path);
        if (!projected.hasValue())
        {
            return projected.status();
        }
        metadata = std::move(*projected);
    }
    else
    {
        auto codec = CodecMetadata::create(
            SourceEncoding{.sampleFormat = format,
                           .byteOrder = format.sizeBytes > 1u ? ByteOrder::Big : ByteOrder::Native},
            {});
        if (!codec.hasValue())
        {
            return codec.status();
        }
        metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    }
    // The payload is validated by decompressing it and by nothing else, so a probe says what it
    // actually established rather than claiming a check it did not make.
    return CodecImageInfo{*descriptor, *layout, 1u, std::move(metadata), CodecProbeDisposition::DecodeRequired};
}

Result<CodecDecodeResult> readPng(const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, PngReadSession& session, CodecImageInfo info)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    auto buffer = SampleBuffer::createOwned(engine, info.decodedLayout, StorageInitialization::Zeroed);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    auto frame = RawFrame::create(info.descriptor, info.decodedLayout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    auto bytes = *frame->mutableBytes();
    RowProgress progress{&execution, static_cast<std::uint64_t>(info.descriptor.extent().height()) *
                                         static_cast<std::uint64_t>(session.passes)};
    const PngDecodeTarget target{bytes.data(), info.decodedLayout.rowStrideBytes(), &progress, &pngRowStep};
    const bool decoded = pngDecodeRows(&session, &target);
    if (session.stopped)
    {
        const auto stopped = execution.checkCancellation();
        return stopped.failed() ? stopped : Status::failure(ErrorCode::Cancelled);
    }
    if (session.truncated)
    {
        return pngFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode PNG pixels", request.path,
                          "the payload ends before the geometry the header declares");
    }
    if (!decoded)
    {
        return pngFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode PNG pixels", request.path,
                          session.message);
    }
    if (request.readMetadata)
    {
        // The chunks after the payload have been read by now, so what a decode states includes the
        // text a writer put at the end of the container and a probe could not have reached.
        auto cards = pngCards(session, true);
        if (!cards.hasValue())
        {
            return cards.status();
        }
        auto projected =
            pngMetadata(std::move(*cards), pngColorDeclaration(session), info.descriptor.sampleFormat(), request.path);
        if (!projected.hasValue())
        {
            return projected.status();
        }
        info.metadata = std::move(*projected);
    }
    return CodecDecodeResult{std::move(*frame), std::move(info.metadata)};
}

} // namespace an
