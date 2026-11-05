// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the camera RAW adapter: its prepared read, the extraction of the sensor
// plane or the full-colour planes, and the factory.
//
// A read is prepared once - opened and described - and the facade checks that description before
// the decode allocates the frame from the engine context and only then asks LibRaw to unpack, so a
// budget refusal comes before the library acquires its own workspace. An ordinary mosaic is
// extracted in one copy-and-subtract pass straight from LibRaw's raw buffer; every other sensor goes
// through LibRaw's own mapping, and only a full-colour sensor through its colour processing. The
// extraction runs on the operation's scheduler and checks cancellation on every row.

#include <AfterNightEngine/IO/RawCodec.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/raw/RawFile.h"
#include "io/registry/PreparedCodecRead.h"

#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace an
{
namespace
{
struct ProcessedImageDeleter
{
    void operator()(libraw_processed_image_t* image) const noexcept
    {
        LibRaw::dcraw_clear_mem(image);
    }
};

struct DirectMosaic
{
    const std::uint16_t* data = nullptr;
    std::size_t stride = 0u;
    // LibRaw's packed CFA repeats over eight rows and two columns. Retain its channel
    // indices, including the two green slots, rather than deriving black from RGB names.
    std::uint32_t black[8][2]{};
};

// Ordinary mosaics need only one copy/subtract pass. Leave unusual geometry,
// residual black tiles and decoder-specific corrections with LibRaw.
DirectMosaic directMosaic(RawDecoder& raw, std::uint32_t width, std::uint32_t height)
{
    raw.raw2image_start();
    const auto& sizes = raw.imgdata.sizes;
    const auto* decoderName = raw.unpack_function_name();
    if (!raw.imgdata.rawdata.raw_image || raw.is_fuji_rotated() || raw.is_phaseone_compressed() ||
        (decoderName && std::strstr(decoderName, "canon_600_load_raw")) || sizes.width != width ||
        sizes.height != height || sizes.iwidth != width || sizes.iheight != height ||
        static_cast<std::uint32_t>(sizes.left_margin) + width > sizes.raw_width ||
        static_cast<std::uint32_t>(sizes.top_margin) + height > sizes.raw_height || sizes.raw_pitch % 2u != 0u ||
        sizes.raw_pitch < static_cast<std::uint32_t>(sizes.raw_width) * 2u)
        return {};
    raw.adjust_bl();
    const auto* levels = raw.imgdata.color.cblack;
    if (levels[4] != 0u && levels[5] != 0u)
        return {};
    const bool uniform = levels[0] == levels[1] && levels[0] == levels[2] && levels[0] == levels[3];
    // The nonuniform extension covers packed Bayer CFAs. Other sensor patterns retain
    // the existing uniform-black path or the library's correction path.
    if (!uniform && raw.imgdata.idata.filters <= 1000u)
        return {};
    DirectMosaic result;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const int channel = uniform ? 0 : raw.COLOR(y, x);
            if (channel < 0 || channel > 3)
                return {};
            result.black[y][x] = levels[channel];
        }
    result.stride = sizes.raw_pitch / 2u;
    result.data =
        raw.imgdata.rawdata.raw_image + static_cast<std::size_t>(sizes.top_margin) * result.stride + sizes.left_margin;
    return result;
}

void copyMosaicRow(const std::uint16_t* source, std::uint16_t* destination, std::uint32_t width,
                   const std::uint32_t (&black)[2]) noexcept
{
    if (black[0] == black[1])
    {
        const auto level = black[0];
        if (level == 0u)
            std::memcpy(destination, source, static_cast<std::size_t>(width) * 2u);
        else
            for (std::uint32_t x = 0u; x < width; ++x)
                destination[x] = source[x] > level ? static_cast<std::uint16_t>(source[x] - level) : 0u;
        return;
    }
    const auto even = black[0];
    const auto odd = black[1];
    std::uint32_t x = 0u;
    for (; x + 1u < width; x += 2u)
    {
        destination[x] = source[x] > even ? static_cast<std::uint16_t>(source[x] - even) : 0u;
        destination[x + 1u] = source[x + 1u] > odd ? static_cast<std::uint16_t>(source[x + 1u] - odd) : 0u;
    }
    if (x < width)
        destination[x] = source[x] > even ? static_cast<std::uint16_t>(source[x] - even) : 0u;
}

class RawPreparedRead final : public PreparedCodecRead
{
public:
    RawPreparedRead(std::unique_ptr<RawFile> file, CodecImageInfo info)
        : m_file(std::move(file)), m_info(std::move(info))
    {
    }
    const CodecImageInfo& info() const noexcept override
    {
        return m_info;
    }

    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request, const CodecRegion* region) override
    {
        if (region != nullptr)
            return Status::failure(ErrorCode::Unsupported);
        if (!m_file)
            return Status::failure(ErrorCode::InvalidArgument);
        auto decoded = evaluateAcrossBoundary(kRawBoundary, [&]() -> Result<CodecDecodeResult> {
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            // Refuse the counted allocation before asking the decoder to acquire its workspace.
            auto buffer = SampleBuffer::createOwned(engine, m_info.decodedLayout, StorageInitialization::Uninitialized);
            if (!buffer.hasValue())
                return buffer.status();
            auto frame = RawFrame::create(m_info.descriptor, m_info.decodedLayout, std::move(*buffer));
            if (!frame.hasValue())
                return frame.status();
            if (const auto checked = unpackRaw(m_file, request); checked.failed())
                return checked;
            auto actualInfo = describeRaw(*m_file, engine, request);
            if (!actualInfo.hasValue())
                return actualInfo.status();
            if (actualInfo->descriptor != m_info.descriptor || actualInfo->decodedLayout != m_info.decodedLayout)
                return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, CodecFormat::Raw,
                                    "validate unpacked RAW description", request.path);
            auto& raw = m_file->decoder;
            // Unpack can discover calibration not available at header time. Capture it before
            // raw2image/subtract_black or colour processing can alter the source values, beside the
            // Exif the file states.
            std::vector<CodecCard> exif;
            if (request.readMetadata)
                exif = rawExifCards(engine, execution, request.path);
            auto metadata = rawMetadata(raw, request.readMetadata, true, std::move(exif));
            if (!metadata.hasValue())
                return metadata.status();
            const auto width = m_info.descriptor.extent().width();
            const auto height = m_info.descriptor.extent().height();
            const auto pixels = static_cast<std::size_t>(width) * height;
            std::unique_ptr<libraw_processed_image_t, ProcessedImageDeleter> processed;
            DirectMosaic sensor;
            const bool fullColor = rawFullColor(raw);
            if (fullColor != (m_info.decodedLayout.planeCount() == 3u))
                return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, CodecFormat::Raw,
                                    "validate RAW sensor layout", request.path);
            if (fullColor)
            {
                if (const auto checked =
                        rawStatus(*m_file, raw.dcraw_process(), CodecFailure::PayloadUnreadable, request.path);
                    checked.failed())
                    return checked;
                int code = LIBRAW_SUCCESS;
                processed.reset(raw.dcraw_make_mem_image(&code));
                if (const auto checked = rawStatus(*m_file, code, CodecFailure::PayloadUnreadable, request.path);
                    checked.failed())
                    return checked;
                if (!processed || processed->type != LIBRAW_IMAGE_BITMAP || processed->bits != 16 ||
                    processed->colors != 3 || processed->width != width || processed->height != height ||
                    processed->data_size != pixels * 6u)
                    return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, CodecFormat::Raw,
                                        "copy full-colour RAW", request.path);
            }
            else if ((sensor = directMosaic(raw, width, height)).data == nullptr)
            {
                // This path never interpolates. LibRaw maps the visible raw_image into its four
                // channel slots and handles sensor-specific geometry/black conventions there.
                if (const auto checked =
                        rawStatus(*m_file, raw.raw2image(), CodecFailure::PayloadUnreadable, request.path);
                    checked.failed())
                    return checked;
                if (const auto checked =
                        rawStatus(*m_file, raw.subtract_black(), CodecFailure::PayloadUnreadable, request.path);
                    checked.failed())
                    return checked;
                if (raw.imgdata.image == nullptr || raw.imgdata.sizes.iwidth != width ||
                    raw.imgdata.sizes.iheight != height)
                    return codecFailure(ErrorCode::Unsupported, CodecFailure::DescriptorMismatch, CodecFormat::Raw,
                                        "extract RAW mosaic", request.path);
            }
            const auto copyStarted = std::chrono::steady_clock::now();
            auto* destination = reinterpret_cast<std::uint16_t*>(frame->mutableBytes()->data());
            const auto copied =
                parallelFor(execution, height, {static_cast<std::uint64_t>(width) * (fullColor ? 12u : 10u), width},
                            [&](TaskRange range) -> Status {
                                for (auto y = range.begin; y < range.end; ++y)
                                {
                                    if (const auto stopped = execution.checkCancellation(); stopped.failed())
                                        return stopped;
                                    if (sensor.data)
                                    {
                                        copyMosaicRow(sensor.data + y * sensor.stride, destination + y * width, width,
                                                      sensor.black[y % 8u]);
                                        if (y % 128u == 127u)
                                            execution.reportTrace(TraceEventKind::Counter, "codec.raw",
                                                                  "copy.checkpoint", static_cast<double>(y), "row");
                                        continue;
                                    }
                                    if (y % 128u == 127u)
                                        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "copy.checkpoint",
                                                              static_cast<double>(y), "row");
                                    for (std::uint32_t x = 0u; x < width; ++x)
                                    {
                                        const auto index = static_cast<std::size_t>(y) * width + x;
                                        if (fullColor)
                                        {
                                            for (unsigned c = 0u; c < 3u; ++c)
                                                std::memcpy(destination + c * pixels + index,
                                                            processed->data + (index * 3u + c) * 2u, 2u);
                                        }
                                        else
                                        {
                                            const int color = raw.imgdata.idata.colors == 1 ? 0
                                                              : raw.is_fuji_rotated()
                                                                  ? raw.FC(static_cast<int>(y), static_cast<int>(x))
                                                                  : raw.COLOR(static_cast<int>(y), static_cast<int>(x));
                                            if (color < 0 || color > 3)
                                                return Status::failure(ErrorCode::InvalidData);
                                            destination[index] = raw.imgdata.image[index][color];
                                        }
                                    }
                                }
                                return {};
                            });
            if (copied.failed())
                return copied;
            execution.reportTrace(
                TraceEventKind::Counter, "codec.raw", "copy",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copyStarted).count(),
                "ms");
            execution.reportProgress(height, height, "decode camera RAW");
            execution.reportTrace(TraceEventKind::Instant, "codec.raw", "publication");
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            return CodecDecodeResult{std::move(*frame), std::move(*metadata)};
        });
        m_file.reset();
        if (!decoded.hasValue())
            return decoded.status();
        return std::move(*decoded);
    }

private:
    std::unique_ptr<RawFile> m_file;
    CodecImageInfo m_info;
};

class RawCodec final : public PreparingImageCodec
{
public:
    explicit RawCodec(const RawCodecOptions& options) : m_options(options) {}
    CodecCapabilities capabilities() const override
    {
        return {CodecFormat::Raw,
                codecSampleBit({2u, RawSampleClass::UnsignedInteger}),
                0u,
                codecLayoutBit(StorageArrangement::Planar),
                codecLayoutBit(StorageArrangement::Planar),
                3u,
                false};
    }
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto options = m_options;
        if (request.rawDecoderPreference)
            options.decoderPreference = *request.rawDecoderPreference;
        if (static_cast<unsigned>(options.decoderPreference) >
            static_cast<unsigned>(RawDecoderPreference::RequireRawSpeed))
            return Status::failure(ErrorCode::InvalidArgument);
        // Keep this snapshot on the prepared file. A fresh native retry deliberately supplies
        // its own options to openRaw, so it cannot acquire the original request's override again.
        auto file = openRaw(execution, request, options);
        if (!file.hasValue())
            return file.status();
        auto described = describeRaw(**file, engine, request);
        if (!described.hasValue())
            return described.status();
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<RawPreparedRead>(std::move(*file), std::move(*described))};
    }
    Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecReadRequest& request) const override
    {
        auto prepared = prepareRead(engine, execution, request);
        if (!prepared.hasValue())
            return prepared.status();
        return (*prepared)->info();
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request) const override
    {
        auto prepared = prepareRead(engine, execution, request);
        if (!prepared.hasValue())
            return prepared.status();
        return (*prepared)->decode(engine, execution, request, nullptr);
    }

private:
    RawCodecOptions m_options;
};
} // namespace

Result<std::shared_ptr<const ImageCodec>> createRawCodec(const RawCodecOptions& options)
{
    if (options.libraryMemoryLimitMegabytes == 0u ||
        static_cast<unsigned>(options.decoderPreference) >
            static_cast<unsigned>(RawDecoderPreference::RequireRawSpeed) ||
        static_cast<unsigned>(options.fullColorWhiteBalance) > static_cast<unsigned>(RawFullColorWhiteBalance::Custom))
        return Status::failure(ErrorCode::InvalidArgument);
    if (options.fullColorWhiteBalance == RawFullColorWhiteBalance::Custom)
        for (const double gain : options.fullColorMultipliers)
            if (!std::isfinite(gain) || gain <= 0.0 || gain > (std::numeric_limits<float>::max)() ||
                static_cast<float>(gain) == 0.0f)
                return Status::failure(ErrorCode::InvalidArgument);
    return evaluateAcrossBoundary(
        kRawBoundary, [&] { return std::shared_ptr<const ImageCodec>{std::make_shared<RawCodec>(options)}; });
}
} // namespace an
