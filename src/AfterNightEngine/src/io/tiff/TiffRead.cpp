// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stream callbacks and per-container handlers every TIFF handle uses, and
// opening, describing and decoding one image of a container.
//
// The library reads and writes through the callbacks here over a file this adapter opens; a seek
// past the end of a container being written grows it with zeroes, as the library's own file
// interface does, and mapping is refused so the library reads instead. On Windows the file is a
// native handle and every transfer the library asks for is one system call. A description refuses the
// photometric interpretations whose samples are not what they appear to be - a palette index, a
// white-is-zero grey, separated inks and chroma not produced by the lossy transform - rather than
// reading them as grey levels, and refuses associated alpha rather than relabelling it. A
// single-channel image whose filter-array tags name red, green and blue in a two-by-two or
// six-by-six block is described as that mosaic. The metadata comes from the Exif the whole file
// states, read before the samples are allocated.

#include "io/tiff/TiffFile.h"

#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "interop/exiv2/ExifCards.h"
#include "io/metadata/MetadataFile.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// File I/O needs no GUI declarations from the Windows SDK.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <Windows.h>
#endif

namespace an
{
namespace
{

#if defined(_WIN32)
/// The most one system call moves; a longer transfer is made in pieces this long.
constexpr tmsize_t kLargestTransfer = tmsize_t{1} << 30;

/// The native handle a container holds.
/// @param file the container.
/// @return its handle, which is open while the library's is.
[[nodiscard]] HANDLE nativeHandle(const TiffFile& file) noexcept
{
    return static_cast<HANDLE>(file.handle);
}
#endif

/// The filter colours a filter-array tag states, in the order that tag numbers them.
constexpr std::uint8_t kCfaRed = 0u;
constexpr std::uint8_t kCfaGreen = 1u;
constexpr std::uint8_t kCfaBlue = 2u;

/// The path a literal UTF-8 string names.
///
/// @param path the boundary path.
/// @return the platform path, decoded through `char8_t` rather than the process code page.
[[nodiscard]] std::filesystem::path filePath(std::string_view path)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

} // namespace

int tiffErrorHandler(TIFF*, void* user, const char*, const char* format, va_list arguments)
{
    auto* const file = static_cast<TiffFile*>(user);
    if (file != nullptr && file->message[0] == '\0' && format != nullptr)
    {
        static_cast<void>(std::vsnprintf(file->message, kTiffMessageBytes, format, arguments));
    }
    return 1;
}

int tiffWarningHandler(TIFF*, void* user, const char*, const char* format, va_list arguments)
{
    auto* const file = static_cast<TiffFile*>(user);
    if (file == nullptr)
    {
        return 1;
    }
    ++file->warnings;
    if (file->execution != nullptr && format != nullptr)
    {
        char text[kTiffMessageBytes]{};
        static_cast<void>(std::vsnprintf(text, sizeof(text), format, arguments));
        file->execution->reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Io, "TIFF codec", text);
    }
    return 1;
}

tmsize_t tiffReadProc(thandle_t handle, void* data, tmsize_t size)
{
    auto* const file = static_cast<TiffFile*>(handle);
#if defined(_WIN32)
    auto* const bytes = static_cast<char*>(data);
    tmsize_t total = 0;
    while (total < size)
    {
        const auto wanted = static_cast<DWORD>((std::min)(size - total, kLargestTransfer));
        DWORD read = 0u;
        if (ReadFile(nativeHandle(*file), bytes + total, wanted, &read, nullptr) == 0 || read == 0u)
        {
            break;
        }
        total += static_cast<tmsize_t>(read);
    }
    return total;
#else
    file->stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    const auto read = static_cast<tmsize_t>(file->stream.gcount());
    file->stream.clear();
    return read;
#endif
}

tmsize_t tiffWriteProc(thandle_t handle, void* data, tmsize_t size)
{
    auto* const file = static_cast<TiffFile*>(handle);
#if defined(_WIN32)
    const auto* const bytes = static_cast<const char*>(data);
    for (tmsize_t total = 0; total < size;)
    {
        const auto wanted = static_cast<DWORD>((std::min)(size - total, kLargestTransfer));
        DWORD written = 0u;
        if (WriteFile(nativeHandle(*file), bytes + total, wanted, &written, nullptr) == 0 || written != wanted)
        {
            file->failed = true;
            return 0;
        }
        total += static_cast<tmsize_t>(written);
    }
    return size;
#else
    file->stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file->stream)
    {
        file->failed = true;
        file->stream.clear();
        return 0;
    }
    return size;
#endif
}

toff_t tiffSeekProc(thandle_t handle, toff_t offset, int whence)
{
    auto* const file = static_cast<TiffFile*>(handle);
#if defined(_WIN32)
    // A position past the end is allowed, and the file system fills the gap a later write leaves
    // with zeroes, as it does for the library's own file interface.
    const DWORD method = whence == SEEK_SET ? FILE_BEGIN : whence == SEEK_CUR ? FILE_CURRENT : FILE_END;
    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(offset);
    LARGE_INTEGER position{};
    if (SetFilePointerEx(nativeHandle(*file), distance, &position, method) == 0)
    {
        file->failed = true;
        return static_cast<toff_t>(-1);
    }
    return static_cast<toff_t>(position.QuadPart);
#else
    const auto direction = whence == SEEK_SET ? std::ios::beg : whence == SEEK_CUR ? std::ios::cur : std::ios::end;
    const auto mode = file->writing ? (std::ios::in | std::ios::out) : std::ios::in;
    file->stream.clear();
    const auto position = file->stream.rdbuf()->pubseekoff(static_cast<std::streamoff>(offset), direction, mode);
    if (position == std::streampos(-1))
    {
        // Writing past the end is how this container grows: the gap is filled by seeking to the end
        // and writing zeroes, which is what the library's own file interface does.
        if (!file->writing || direction != std::ios::beg)
        {
            file->failed = true;
            return static_cast<toff_t>(-1);
        }
        file->stream.clear();
        const auto end = file->stream.rdbuf()->pubseekoff(0, std::ios::end, mode);
        if (end == std::streampos(-1) || static_cast<toff_t>(end) > offset)
        {
            file->failed = true;
            return static_cast<toff_t>(-1);
        }
        const char zero = '\0';
        for (auto written = static_cast<toff_t>(end); written < offset; ++written)
        {
            file->stream.write(&zero, 1);
        }
        if (!file->stream)
        {
            file->failed = true;
            return static_cast<toff_t>(-1);
        }
        return offset;
    }
    return static_cast<toff_t>(position);
#endif
}

int tiffCloseProc(thandle_t)
{
    return 0;
}

toff_t tiffSizeProc(thandle_t handle)
{
    auto* const file = static_cast<TiffFile*>(handle);
#if defined(_WIN32)
    LARGE_INTEGER size{};
    return GetFileSizeEx(nativeHandle(*file), &size) != 0 && size.QuadPart >= 0 ? static_cast<toff_t>(size.QuadPart)
                                                                                : 0u;
#else
    const auto mode = file->writing ? (std::ios::in | std::ios::out) : std::ios::in;
    file->stream.clear();
    const auto current = file->stream.rdbuf()->pubseekoff(0, std::ios::cur, mode);
    const auto end = file->stream.rdbuf()->pubseekoff(0, std::ios::end, mode);
    static_cast<void>(file->stream.rdbuf()->pubseekoff(current, std::ios::beg, mode));
    return end == std::streampos(-1) ? 0u : static_cast<toff_t>(end);
#endif
}

int tiffMapProc(thandle_t, void**, toff_t*)
{
    return 0;
}

void tiffUnmapProc(thandle_t, void*, toff_t) {}

namespace
{

/// A block of counted engine memory, released however the read ends.
class CountedBuffer
{
public:
    /// Takes nothing.
    CountedBuffer() = default;

    /// Releases the block.
    ~CountedBuffer()
    {
        if (m_bytes != nullptr && m_resource != nullptr)
        {
            m_resource->deallocate(m_bytes, m_size, kAlignment);
        }
    }

    CountedBuffer(const CountedBuffer&) = delete;
    CountedBuffer& operator=(const CountedBuffer&) = delete;
    CountedBuffer(CountedBuffer&&) = delete;
    CountedBuffer& operator=(CountedBuffer&&) = delete;

    /// Takes a block from the engine context's budget.
    ///
    /// @param engine the context whose budget pays for it.
    /// @param size how many bytes are wanted.
    /// @return success, or the budget's refusal.
    [[nodiscard]] Status acquire(const EngineContext& engine, std::uint64_t size)
    {
        MemoryResource* const resource = engine.memoryResource();
        if (resource == nullptr)
        {
            return Status::failure(ErrorCode::NotInitialized);
        }
        auto memory = resource->allocate(size, kAlignment);
        if (!memory.hasValue())
        {
            return memory.status();
        }
        m_resource = resource;
        m_bytes = *memory;
        m_size = size;
        return {};
    }

    /// The block.
    ///
    /// @return its first byte, or null when nothing was acquired.
    [[nodiscard]] std::byte* bytes() const noexcept
    {
        return static_cast<std::byte*>(m_bytes);
    }

    /// How long it is.
    ///
    /// @return the size in bytes.
    [[nodiscard]] std::uint64_t size() const noexcept
    {
        return m_size;
    }

private:
    /// What a sample of any width may be read from.
    static constexpr std::uint64_t kAlignment = 64u;

    /// Who to give the memory back to.
    MemoryResource* m_resource = nullptr;

    /// The block.
    void* m_bytes = nullptr;

    /// How long it is.
    std::uint64_t m_size = 0u;
};

/// The representation a stated sample format and width are.
///
/// @param facts what the image states about itself.
/// @return the boundary representation, or nothing for a combination this engine does not carry.
[[nodiscard]] std::optional<RawSampleFormat> tiffSampleFormat(const TiffImageFacts& facts) noexcept
{
    if (facts.bitsPerSample == 0u || facts.bitsPerSample % 8u != 0u)
    {
        return std::nullopt;
    }
    const auto sizeBytes = static_cast<std::uint32_t>(facts.bitsPerSample / 8u);
    if (!isRawSampleSize(sizeBytes) || sizeBytes > 8u)
    {
        return std::nullopt;
    }
    switch (facts.sampleFormat)
    {
    case SAMPLEFORMAT_UINT:
        return RawSampleFormat{sizeBytes, RawSampleClass::UnsignedInteger};
    case SAMPLEFORMAT_INT:
        return RawSampleFormat{sizeBytes, RawSampleClass::SignedInteger};
    case SAMPLEFORMAT_IEEEFP:
        // Half, single and double precision are floating widths; one byte of floating point is not
        // a representation any container writes.
        return sizeBytes == 1u
                   ? std::nullopt
                   : std::optional<RawSampleFormat>{RawSampleFormat{sizeBytes, RawSampleClass::FloatingPoint}};
    default:
        break;
    }
    return std::nullopt;
}

/// The filter array a container's own tags state.
///
/// @param tiff the open container, positioned at the selected image.
/// @param facts what that image states about itself.
/// @return the descriptor, or nothing when the image states no array this engine names.
[[nodiscard]] std::optional<CfaDescriptor> tiffFilterArray(TIFF* tiff, const TiffImageFacts& facts)
{
    if (facts.samplesPerPixel != 1u)
    {
        return std::nullopt;
    }
    std::uint16_t* block = nullptr;
    std::uint16_t patternCount = 0u;
    std::uint8_t* pattern = nullptr;
    if (TIFFGetField(tiff, TIFFTAG_CFAREPEATPATTERNDIM, &block) != 1 || block == nullptr ||
        TIFFGetField(tiff, TIFFTAG_CFAPATTERN, &patternCount, &pattern) != 1 || pattern == nullptr)
    {
        return std::nullopt;
    }
    if (block[0] != block[1] || patternCount != static_cast<std::uint32_t>(block[0]) * block[1])
    {
        return std::nullopt;
    }
    const auto colorOf = [](std::uint8_t code) noexcept -> std::optional<CfaColor> {
        if (code == kCfaRed)
        {
            return CfaColor::Red;
        }
        if (code == kCfaGreen)
        {
            return CfaColor::Green;
        }
        if (code == kCfaBlue)
        {
            return CfaColor::Blue;
        }
        return std::nullopt;
    };
    if (block[0] == 2u)
    {
        const auto first = colorOf(pattern[0]);
        const auto second = colorOf(pattern[1]);
        if (!first || !second)
        {
            return std::nullopt;
        }
        if (*first == CfaColor::Red)
        {
            return CfaDescriptor::create(CfaPattern::Rggb, 0u, 0u);
        }
        if (*first == CfaColor::Blue)
        {
            return CfaDescriptor::create(CfaPattern::Bggr, 0u, 0u);
        }
        return *second == CfaColor::Red ? CfaDescriptor::create(CfaPattern::Grbg, 0u, 0u)
                                        : CfaDescriptor::create(CfaPattern::Gbrg, 0u, 0u);
    }
    if (block[0] != kCfaTableSide)
    {
        return std::nullopt;
    }
    CfaColorEntries entries{};
    for (std::size_t index = 0u; index < static_cast<std::size_t>(kCfaTableSide) * kCfaTableSide; ++index)
    {
        const auto color = colorOf(pattern[index]);
        if (!color)
        {
            return std::nullopt;
        }
        entries[index] = *color;
    }
    return CfaDescriptor::create(entries, 0u, 0u);
}

} // namespace

void TiffFileDeleter::operator()(TiffFile* file) const noexcept
{
    if (file == nullptr)
    {
        return;
    }
    if (file->tiff != nullptr)
    {
        TIFFClose(file->tiff);
    }
    delete file;
}

TiffFile::~TiffFile()
{
#if defined(_WIN32)
    if (handle != nullptr)
    {
        CloseHandle(nativeHandle(*this));
    }
#endif
}

bool openTiffFile(TiffFile& file, std::string_view path, bool writing)
{
    file.writing = writing;
#if defined(_WIN32)
    // The access and sharing a C library stream opened for "r" or "w+" asks for.
    const HANDLE opened = CreateFileW(filePath(path).c_str(), writing ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      writing ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (opened == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    file.handle = opened;
    return true;
#else
    file.stream.open(filePath(path), writing ? std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc
                                             : std::ios::binary | std::ios::in);
    return file.stream.is_open();
#endif
}

bool readTiffContainer(std::string_view path, std::byte* bytes, std::uint64_t size)
{
    TiffFile file;
    return openTiffFile(file, path, false) &&
           tiffReadProc(&file, bytes, static_cast<tmsize_t>(size)) == static_cast<tmsize_t>(size);
}

bool flushTiffFile(TiffFile& file)
{
#if defined(_WIN32)
    // A native write is in the operating system's hands when it returns; nothing is held here.
    return !file.failed;
#else
    file.stream.flush();
    if (!file.stream)
    {
        file.failed = true;
    }
    return !file.failed;
#endif
}

Status tiffFailure(ErrorCode code, CodecFailure reason, std::string_view operation, std::string_view path,
                   std::string_view message)
{
    return codecFailure(
        code, reason, CodecFormat::Tiff, operation, path,
        Status::failure(
            code, {.message = std::string(message), .operation = std::string(operation), .domain = ErrorDomain::Io}));
}

Result<TiffHandle> openTiffRead(const EngineContext& engine, const ExecutionContext& execution, std::string_view path)
{
    auto opened = evaluateAcrossBoundary(kTiffBoundary, [&]() -> TiffHandle {
        TiffHandle file{new TiffFile()};
        file->execution = &execution;
        return openTiffFile(*file, path, false) ? std::move(file) : TiffHandle{};
    });
    if (!opened.hasValue())
    {
        return codecFailure(opened.status().code(), CodecFailure::MetadataUnreadable, CodecFormat::Tiff, "open TIFF",
                            path, opened.status());
    }
    TiffHandle file = std::move(*opened);
    if (file == nullptr)
    {
        return tiffFailure(ErrorCode::ExternalFailure, CodecFailure::MetadataUnreadable, "open TIFF", path,
                           "the file could not be opened for reading");
    }
    TIFFOpenOptions* const options = TIFFOpenOptionsAlloc();
    if (options == nullptr)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
    TIFFOpenOptionsSetErrorHandlerExtR(options, &tiffErrorHandler, file.get());
    TIFFOpenOptionsSetWarningHandlerExtR(options, &tiffWarningHandler, file.get());
    TIFFOpenOptionsSetMaxSingleMemAlloc(options, static_cast<tmsize_t>(engine.limits().maxImageBytes));
    file->tiff = TIFFClientOpenExt("image", "r", file.get(), &tiffReadProc, &tiffWriteProc, &tiffSeekProc,
                                   &tiffCloseProc, &tiffSizeProc, &tiffMapProc, &tiffUnmapProc, options);
    TIFFOpenOptionsFree(options);
    if (file->tiff == nullptr)
    {
        return tiffFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "open TIFF", path,
                           file->message[0] != '\0' ? file->message : "the file is not a TIFF container");
    }
    return file;
}

Result<TiffImageFacts> readTiffFacts(TiffFile& file, std::uint32_t imageIndex, std::string_view path)
{
    if (imageIndex > 0u && TIFFSetDirectory(file.tiff, static_cast<tdir_t>(imageIndex)) != 1)
    {
        return Status::failure(ErrorCode::OutOfRange);
    }
    TiffImageFacts facts;
    if (TIFFGetField(file.tiff, TIFFTAG_IMAGEWIDTH, &facts.width) != 1 ||
        TIFFGetField(file.tiff, TIFFTAG_IMAGELENGTH, &facts.height) != 1 ||
        TIFFGetField(file.tiff, TIFFTAG_PHOTOMETRIC, &facts.photometric) != 1)
    {
        return tiffFailure(ErrorCode::InvalidData, CodecFailure::MetadataUnreadable, "describe TIFF image", path,
                           "the image states no geometry or no photometric interpretation");
    }
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_BITSPERSAMPLE, &facts.bitsPerSample);
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_SAMPLESPERPIXEL, &facts.samplesPerPixel);
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_SAMPLEFORMAT, &facts.sampleFormat);
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_PLANARCONFIG, &facts.planarConfig);
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_ORIENTATION, &facts.orientation);
    TIFFGetFieldDefaulted(file.tiff, TIFFTAG_COMPRESSION, &facts.compression);
    std::uint16_t extraCount = 0u;
    std::uint16_t* extraTypes = nullptr;
    if (TIFFGetField(file.tiff, TIFFTAG_EXTRASAMPLES, &extraCount, &extraTypes) == 1)
    {
        facts.extraSamples = extraCount;
        if (extraCount > 0u && extraTypes != nullptr)
        {
            facts.firstExtraSample = extraTypes[0];
            facts.associatedAlpha =
                std::find(extraTypes, extraTypes + extraCount, EXTRASAMPLE_ASSOCALPHA) != extraTypes + extraCount;
        }
    }
    std::uint32_t profileBytes = 0u;
    void* profile = nullptr;
    facts.profile = TIFFGetField(file.tiff, TIFFTAG_ICCPROFILE, &profileBytes, &profile) == 1 && profileBytes > 0u;
    facts.tiled = TIFFIsTiled(file.tiff) != 0;
    return facts;
}

Result<std::vector<CodecCard>> readTiffCards(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path)
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    // Exif offsets address the whole file, but parsing metadata need not read or copy its pixels.
    auto file = MetadataFile::open(engine, path, CodecFormat::Tiff, "TIFF");
    if (!file.hasValue())
    {
        return file.status();
    }
    return readTiffExifCards(file->bytes());
}

Result<ImageMetadata> tiffMetadata(std::vector<CodecCard> cards, const TiffImageFacts& facts,
                                   const RawSampleFormat& sampleFormat, ByteOrder fileByteOrder, std::string_view path)
{
    static_cast<void>(facts);
    auto codec = CodecMetadata::create(SourceEncoding{.sampleFormat = sampleFormat, .byteOrder = fileByteOrder},
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
    auto provenance = Provenance::create(
        ProvenanceSourceKind::File,
        path.size() <= kMaxProvenanceTextBytes ? std::string(path) : "TIFF source path exceeds lineage text bound");
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    // This container states no transfer function: it has a tag for an embedded profile and none for
    // a curve, so what an image has been through is not something the file says.
    return ImageMetadata::create(std::move(*acquisition), {}, {}, std::move(*provenance), std::move(*codec),
                                 std::nullopt);
}

Result<CodecImageInfo> describeTiff(TiffFile& file, const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecReadRequest& request)
{
    auto facts = readTiffFacts(file, request.imageIndex, request.path);
    if (!facts.hasValue())
    {
        return facts.status();
    }
    const bool jpegColour = facts->photometric == PHOTOMETRIC_YCBCR && facts->compression == COMPRESSION_JPEG;
    if (jpegColour)
    {
        // The library's own conversion is what decoding this payload is: the samples are stored as
        // luma and chroma because the transform that produced them says so, and every reader of
        // such a file obtains them as red, green and blue.
        TIFFSetField(file.tiff, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
    }
    if (facts->photometric != PHOTOMETRIC_MINISBLACK && facts->photometric != PHOTOMETRIC_RGB && !jpegColour)
    {
        return tiffFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe TIFF image",
                           request.path,
                           "this image states a photometric interpretation whose samples are not what they appear to "
                           "be, and this engine refuses it rather than reading them as grey levels");
    }
    const auto format = tiffSampleFormat(*facts);
    if (!format || facts->samplesPerPixel == 0u)
    {
        return tiffFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe TIFF image",
                           request.path, "this image states a sample representation this engine does not carry");
    }
    const auto limits = engine.limits();
    const auto channels = static_cast<std::uint32_t>(facts->samplesPerPixel);
    const auto extent = ImageExtent::create(facts->width, facts->height, channels, limits);
    if (!extent)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    if (facts->associatedAlpha)
        return tiffFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, "describe TIFF image",
                           request.path, "associated alpha requires an explicit association contract before decoding");
    const bool alpha = facts->extraSamples == 1u && facts->firstExtraSample == EXTRASAMPLE_UNASSALPHA;
    const bool colour = facts->photometric == PHOTOMETRIC_RGB || jpegColour;
    const auto schema = channels == 1u                      ? ChannelSchema::Mono
                        : colour && channels == 3u          ? ChannelSchema::Rgb
                        : colour && channels == 4u && alpha ? ChannelSchema::Rgba
                                                            : ChannelSchema::Named;
    const auto cfa = schema == ChannelSchema::Mono ? tiffFilterArray(file.tiff, *facts) : std::nullopt;
    // The container states nothing about a curve. An embedded profile and a lossy transform each
    // say the samples were prepared for a display; everything else is read as a plain fraction of
    // full scale, which is what an interchange file of linear data is.
    const auto domain =
        facts->profile || jpegColour ? NumericDomain::EncodedNonlinear : NumericDomain::NormalizedLinear;
    const auto origin = facts->orientation == ORIENTATION_BOTLEFT || facts->orientation == ORIENTATION_BOTRIGHT
                            ? CoordinateOrigin::BottomLeft
                            : CoordinateOrigin::TopLeft;
    const auto descriptor = RawFrameDescriptor::create({.extent = *extent,
                                                        .sampleFormat = *format,
                                                        .byteOrder = ByteOrder::Native,
                                                        .channelSchema = schema,
                                                        .colorSpace = ColorSpace::Unspecified,
                                                        .numericDomain = domain,
                                                        .cfa = cfa.value_or(CfaDescriptor{}),
                                                        .coordinateOrigin = origin,
                                                        .pixelAspectRatio = 1.0});
    if (!descriptor)
    {
        return Status::failure(ErrorCode::InvalidData);
    }
    const bool separate = facts->planarConfig == PLANARCONFIG_SEPARATE && channels > 1u;
    const auto layout = ImageLayout::create({.extent = *extent,
                                             .elementSizeBytes = format->sizeBytes,
                                             .samplesPerPixel = separate ? 1u : channels,
                                             .planeCount = separate ? channels : 1u,
                                             .rowAlignmentBytes = 1u},
                                            limits);
    if (!layout)
    {
        return Status::failure(ErrorCode::LimitExceeded);
    }
    const auto fileByteOrder = TIFFIsByteSwapped(file.tiff) != 0
                                   ? (nativeByteOrder() == ByteOrder::Big ? ByteOrder::Little : ByteOrder::Big)
                                   : nativeByteOrder();
    ImageMetadata metadata;
    if (request.readMetadata)
    {
        auto cards = readTiffCards(engine, execution, request.path);
        if (!cards.hasValue())
        {
            return cards.status();
        }
        auto projected = tiffMetadata(std::move(*cards), *facts, *format, fileByteOrder, request.path);
        if (!projected.hasValue())
        {
            return projected.status();
        }
        metadata = std::move(*projected);
    }
    else
    {
        auto codec = CodecMetadata::create(SourceEncoding{.sampleFormat = *format, .byteOrder = fileByteOrder}, {});
        if (!codec.hasValue())
        {
            return codec.status();
        }
        metadata = ImageMetadata::create({}, {}, {}, {}, std::move(*codec), std::nullopt);
    }
    const auto images = static_cast<std::uint64_t>(TIFFNumberOfDirectories(file.tiff));
    // A payload is validated by decoding it: a strip's length says how many bytes it holds, not
    // whether they decompress.
    return CodecImageInfo{*descriptor, *layout, images == 0u ? 1u : images, std::move(metadata),
                          CodecProbeDisposition::DecodeRequired};
}

Result<CodecDecodeResult> readTiff(const EngineContext& engine, const ExecutionContext& execution,
                                   const CodecReadRequest& request, TiffFile& file, CodecImageInfo info)
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
    const auto layout = info.decodedLayout;
    const auto height = info.descriptor.extent().height();
    const auto width = info.descriptor.extent().width();
    const auto planes = layout.planeCount();
    const std::uint64_t total = static_cast<std::uint64_t>(height) * planes;
    std::uint64_t completed = 0u;
    // The body returns a status, which a value-carrying boundary cannot hold: a result that carried
    // a failure in both of its halves would have two answers. The boundary wraps a call that
    // returns nothing and the body's own answer comes back beside it.
    const auto body = [&]() -> Status {
        if (TIFFIsTiled(file.tiff) != 0)
        {
            std::uint32_t tileWidth = 0u;
            std::uint32_t tileHeight = 0u;
            if (TIFFGetField(file.tiff, TIFFTAG_TILEWIDTH, &tileWidth) != 1 ||
                TIFFGetField(file.tiff, TIFFTAG_TILELENGTH, &tileHeight) != 1 || tileWidth == 0u || tileHeight == 0u)
            {
                return tiffFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode TIFF pixels",
                                   request.path, "the image states tiles without stating their size");
            }
            const auto tileBytes = TIFFTileSize(file.tiff);
            if (tileBytes <= 0)
            {
                return tiffFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode TIFF pixels",
                                   request.path, "the image states a tile of no size");
            }
            CountedBuffer tile;
            if (const auto acquired = tile.acquire(engine, static_cast<std::uint64_t>(tileBytes)); acquired.failed())
            {
                return acquired;
            }
            const auto sampleBytes = layout.elementSizeBytes() * layout.samplesPerPixel();
            for (std::uint32_t plane = 0u; plane < planes; ++plane)
            {
                for (std::uint32_t top = 0u; top < height; top += tileHeight)
                {
                    for (std::uint32_t left = 0u; left < width; left += tileWidth)
                    {
                        if (const auto stopped = execution.checkCancellation(); stopped.failed())
                        {
                            return stopped;
                        }
                        const auto index = TIFFComputeTile(file.tiff, left, top, 0u, static_cast<std::uint16_t>(plane));
                        if (TIFFReadEncodedTile(file.tiff, index, tile.bytes(), tileBytes) < 0)
                        {
                            return tiffFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable,
                                               "decode TIFF pixels", request.path,
                                               file.message[0] != '\0' ? file.message : "a tile could not be decoded");
                        }
                        const auto rows = (std::min)(tileHeight, height - top);
                        const auto columns = (std::min)(tileWidth, width - left);
                        for (std::uint32_t y = 0u; y < rows; ++y)
                        {
                            std::memcpy(bytes.data() + plane * layout.planeStrideBytes() +
                                            (top + y) * layout.rowStrideBytes() +
                                            static_cast<std::uint64_t>(left) * sampleBytes,
                                        tile.bytes() + static_cast<std::size_t>(y) * tileWidth * sampleBytes,
                                        static_cast<std::size_t>(columns) * sampleBytes);
                        }
                    }
                }
                completed += height;
                execution.reportProgress(completed, total, "decode TIFF");
            }
            return {};
        }
        for (std::uint32_t plane = 0u; plane < planes; ++plane)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                if (const auto stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
                auto* const row = bytes.data() + plane * layout.planeStrideBytes() + y * layout.rowStrideBytes();
                if (TIFFReadScanline(file.tiff, row, y, static_cast<std::uint16_t>(plane)) != 1)
                {
                    return tiffFailure(ErrorCode::InvalidData, CodecFailure::PayloadUnreadable, "decode TIFF pixels",
                                       request.path,
                                       file.message[0] != '\0' ? file.message : "a row could not be decoded");
                }
                ++completed;
                execution.reportProgress(completed, total, "decode TIFF");
            }
        }
        return {};
    };
    Status outcome;
    if (const auto thrown = callAcrossBoundary(kTiffBoundary, [&] { outcome = body(); }); thrown.failed())
    {
        return thrown;
    }
    if (outcome.failed())
    {
        return outcome;
    }
    return CodecDecodeResult{std::move(*frame), std::move(info.metadata)};
}

} // namespace an
