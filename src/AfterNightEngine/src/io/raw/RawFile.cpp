// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening a camera RAW file with LibRaw and describing the frame it holds.
//
// The file is opened with this read's callbacks and a fixed set of processing parameters - no
// rotation, no flip, identity gamma, 16-bit output, no automatic brightening - so the only colour
// decision left is the full-colour white balance the options state. LibRaw's return codes and its
// data-error callback map onto engine failures here. The description, filter array included, is
// built before any pixel is allocated, and a filter array it cannot state exactly is refused.

#include "io/raw/RawFile.h"

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "runtime/DependencyVersions.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace an
{
namespace
{
int progress(void* context, LibRaw_progress, int, int) noexcept
{
    const auto& file = *static_cast<RawFile*>(context);
    return file.execution->checkCancellation().failed() ? 1 : 0;
}
void dataError(void* context, const char*, INT64) noexcept
{
    static_cast<RawFile*>(context)->corrupt.store(true, std::memory_order_relaxed);
}

std::optional<CfaDescriptor> filterArray(LibRaw& raw)
{
    if (rawFullColor(raw) || raw.imgdata.idata.colors == 1)
        return CfaDescriptor{};
    if (raw.imgdata.idata.filters == 9u)
    {
        CfaColorEntries entries{};
        for (unsigned y = 0u; y < 6u; ++y)
            for (unsigned x = 0u; x < 6u; ++x)
            {
                const int color = raw.imgdata.idata.xtrans[y][x];
                if (color < 0 || color > 2)
                    return std::nullopt;
                entries[y * 6u + x] = color == 0 ? CfaColor::Red : color == 1 ? CfaColor::Green : CfaColor::Blue;
            }
        return CfaDescriptor::create(entries, 0u, 0u);
    }
    std::string pattern;
    const bool fujiRotated = raw.is_fuji_rotated() != 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            // A SuperCCD is copied into the rectangular output grid by raw2image. FC names the
            // channel at those output coordinates; COLOR applies the diagonal input transform and
            // is only correct while addressing the sensor's encoded grid.
            const int color = fujiRotated ? raw.FC(y, x) : raw.COLOR(y, x);
            if (color < 0 || color > 3)
                return std::nullopt;
            const char letter = raw.imgdata.idata.cdesc[color];
            if (letter != 'R' && letter != 'G' && letter != 'B')
                return std::nullopt;
            pattern += letter;
        }
    // Confirm the entire packed Bayer period; four-colour/other arrays cannot be approximated.
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const int color = fujiRotated ? raw.FC(y, x) : raw.COLOR(y, x);
            if (color < 0 || color > 3 || raw.imgdata.idata.cdesc[color] != pattern[(y % 2) * 2 + x])
                return std::nullopt;
        }
    if (pattern == "RGGB")
        return CfaDescriptor::create(CfaPattern::Rggb, 0u, 0u);
    if (pattern == "BGGR")
        return CfaDescriptor::create(CfaPattern::Bggr, 0u, 0u);
    if (pattern == "GRBG")
        return CfaDescriptor::create(CfaPattern::Grbg, 0u, 0u);
    if (pattern == "GBRG")
        return CfaDescriptor::create(CfaPattern::Gbrg, 0u, 0u);
    return std::nullopt;
}
} // namespace

bool rawFullColor(const LibRaw& decoder) noexcept
{
    return decoder.imgdata.idata.colors != 1 &&
           (decoder.imgdata.idata.is_foveon || decoder.imgdata.idata.filters == 0u ||
            decoder.imgdata.idata.filters == 1u);
}

Status rawStatus(const RawFile& file, int code, CodecFailure reason, std::string_view path)
{
    if (const auto stopped = file.execution->checkCancellation(); stopped.failed())
        return stopped;
    if (code == LIBRAW_SUCCESS && !file.corrupt.load(std::memory_order_relaxed))
        return {};
    if (code == LIBRAW_SUCCESS)
        code = LIBRAW_DATA_ERROR;
    ErrorCode mapped = ErrorCode::InvalidData;
    if (code == LIBRAW_UNSUFFICIENT_MEMORY || code == LIBRAW_MEMPOOL_OVERFLOW)
        mapped = ErrorCode::AllocationFailed;
    else if (code == LIBRAW_FILE_UNSUPPORTED || code == LIBRAW_NOT_IMPLEMENTED)
        mapped = ErrorCode::Unsupported;
    else if (code == LIBRAW_TOO_BIG)
        mapped = ErrorCode::LimitExceeded;
    else if (code == LIBRAW_IO_ERROR)
        mapped = ErrorCode::ExternalFailure;
    return codecFailure(mapped, reason, CodecFormat::Raw, kRawBoundary.operation, path,
                        translateExternalCode(kRawBoundary, mapped, code, LibRaw::strerror(code)));
}

Result<std::unique_ptr<RawFile>> openRaw(const ExecutionContext& execution, const CodecReadRequest& request,
                                         const RawCodecOptions& options)
{
    auto opened = evaluateAcrossBoundary(kRawBoundary, [&]() -> Result<std::unique_ptr<RawFile>> {
        auto file = std::make_unique<RawFile>();
        file->execution = &execution;
        file->options = options;
        auto& raw = file->decoder;
        raw.set_progress_handler(progress, file.get());
        raw.set_dataerror_handler(dataError, file.get());
        raw.imgdata.rawparams.shot_select = request.imageIndex;
        raw.imgdata.rawparams.use_rawspeed = 0;
        raw.imgdata.rawparams.max_raw_memory_mb = options.libraryMemoryLimitMegabytes;
        auto& params = raw.imgdata.params;
        params.use_fuji_rotate = 0;
        params.user_flip = 0;
        params.gamm[0] = params.gamm[1] = 1.0;
        params.no_auto_bright = 1;
        params.output_bps = 16;
        params.output_color = 1;
        params.use_camera_wb = options.fullColorWhiteBalance == RawFullColorWhiteBalance::Camera;
        params.use_auto_wb = options.fullColorWhiteBalance == RawFullColorWhiteBalance::Automatic;
        if (options.fullColorWhiteBalance == RawFullColorWhiteBalance::None)
            for (float& multiplier : params.user_mul)
                multiplier = 1.0f;
        if (options.fullColorWhiteBalance == RawFullColorWhiteBalance::Custom)
            for (unsigned c = 0u; c < 4u; ++c)
                params.user_mul[c] = static_cast<float>(options.fullColorMultipliers[c == 3u ? 1u : c]);
        const auto path = std::filesystem::path(
            std::u8string_view(reinterpret_cast<const char8_t*>(request.path.data()), request.path.size()));
        const auto started = std::chrono::steady_clock::now();
        const int code = raw.open_file(path.c_str());
        execution.reportTrace(
            TraceEventKind::Counter, "codec.raw", "open",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(), "ms");
        if (const auto checked = rawStatus(*file, code, CodecFailure::MetadataUnreadable, request.path);
            checked.failed())
            return checked;
        if (request.imageIndex >= raw.imgdata.idata.raw_count)
            return Status::failure(ErrorCode::OutOfRange);
        return file;
    });
    if (!opened.hasValue())
        return opened.status();
    return std::move(*opened);
}

Result<CodecImageInfo> describeRaw(RawFile& file, const EngineContext& engine, const CodecReadRequest& request)
{
    auto& raw = file.decoder;
    const bool fullColor = rawFullColor(raw);
    const auto& sizes = raw.imgdata.sizes;
    const auto cfa = filterArray(raw);
    if (!cfa)
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, CodecFormat::Raw,
                            "describe RAW filter array", request.path);
    const auto extent = ImageExtent::create(sizes.width, sizes.height, fullColor ? 3u : 1u, engine.limits());
    if (!extent)
        return Status::failure(ErrorCode::LimitExceeded);
    const auto descriptor = RawFrameDescriptor::create(
        {.extent = *extent,
         .sampleFormat = {2u, RawSampleClass::UnsignedInteger},
         .byteOrder = ByteOrder::Native,
         .channelSchema = fullColor ? ChannelSchema::Rgb : ChannelSchema::Mono,
         .colorSpace = fullColor ? ColorSpace::LinearRgb : ColorSpace::Unspecified,
         .numericDomain = fullColor ? NumericDomain::NormalizedLinear : NumericDomain::RawSensorCode,
         .cfa = *cfa,
         .coordinateOrigin = CoordinateOrigin::TopLeft,
         .pixelAspectRatio = sizes.pixel_aspect});
    if (!descriptor)
        return Status::failure(ErrorCode::InvalidData);
    const auto layout = ImageLayout::create({.extent = *extent,
                                             .elementSizeBytes = 2u,
                                             .samplesPerPixel = 1u,
                                             .planeCount = fullColor ? 3u : 1u,
                                             .rowAlignmentBytes = 1u},
                                            engine.limits());
    if (!layout)
        return Status::failure(ErrorCode::LimitExceeded);
    auto metadata = rawMetadata(raw, request.readMetadata, false, {});
    if (!metadata.hasValue())
        return metadata.status();
    return CodecImageInfo{*descriptor, *layout, raw.imgdata.idata.raw_count, std::move(*metadata),
                          CodecProbeDisposition::DecodeRequired};
}

namespace dependency_detail
{

void reportLibRaw(DependencyVersion& entry) noexcept
{
    entry.name = "libraw";
    // Windows links the decoder as a library of its own, under either port; on Linux it is a static
    // archive, because its own decoder dependencies follow it onto the link line there.
#if defined(_WIN32)
    entry.linkage = DependencyLinkage::SharedLibrary;
#else
    entry.linkage = DependencyLinkage::StaticArchive;
#endif

    // Both spellings carry a build tail - `0.22.1-Release` - and the same bounded scan reads both.
    setVersionTextFromBanner(entry.compiled, LIBRAW_VERSION_STR, nullptr);
    setVersionTextFromBanner(entry.loaded, LibRaw::version(), nullptr);
}

} // namespace dependency_detail

} // namespace an
