// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "VideoFixture.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

namespace an::test
{
namespace
{
struct OutputDeleter
{
    void operator()(AVFormatContext* context) const noexcept
    {
        if (context == nullptr)
            return;
        if (context->pb != nullptr && (context->oformat->flags & AVFMT_NOFILE) == 0)
            avio_closep(&context->pb);
        avformat_free_context(context);
    }
};

struct EncoderDeleter
{
    void operator()(AVCodecContext* context) const noexcept
    {
        avcodec_free_context(&context);
    }
};

struct FrameDeleter
{
    void operator()(AVFrame* frame) const noexcept
    {
        av_frame_free(&frame);
    }
};

struct PacketDeleter
{
    void operator()(AVPacket* packet) const noexcept
    {
        av_packet_free(&packet);
    }
};

struct Stream
{
    std::unique_ptr<AVCodecContext, EncoderDeleter> encoder;
    AVStream* stream = nullptr;
    AVPixelFormat format = AV_PIX_FMT_NONE;
};

std::string describe(const char* what, int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return std::string(what) + ": " + text;
}

bool isChromaPlane(const AVPixFmtDescriptor& descriptor, int plane)
{
    return (plane == 1 || plane == 2) && (descriptor.flags & AV_PIX_FMT_FLAG_RGB) == 0 && descriptor.nb_components >= 3;
}

int depthOfPlane(const AVPixFmtDescriptor& descriptor, int plane)
{
    int depth = 0;
    for (int component = 0; component < descriptor.nb_components; ++component)
        if (descriptor.comp[component].plane == plane)
            depth = (std::max)(depth, descriptor.comp[component].depth);
    return depth;
}

void fillPrimary(AVFrame& frame, const AVPixFmtDescriptor& descriptor, const VideoFixtureSpec& spec, int index)
{
    if ((descriptor.flags & AV_PIX_FMT_FLAG_PAL) != 0)
    {
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x)
                frame.data[0][y * frame.linesize[0] + x] = paletteIndex(x, y, index);
        auto* palette = static_cast<std::uint8_t*>(frame.data[1]);
        for (int entry = 0; entry < 256; ++entry)
        {
            const int stored = spec.varyPalette ? (entry + 17 * index) % 256 : entry;
            const std::uint32_t value = spec.zeroPalette ? 0u : paletteEntry(stored);
            std::memcpy(palette + 4 * entry, &value, 4u);
        }
        return;
    }
    const auto format = static_cast<AVPixelFormat>(frame.format);
    const bool bigEndian = (descriptor.flags & AV_PIX_FMT_FLAG_BE) != 0;
    for (int plane = 0; plane < 4 && frame.data[plane] != nullptr; ++plane)
    {
        const int height =
            isChromaPlane(descriptor, plane) ? AV_CEIL_RSHIFT(frame.height, descriptor.log2_chroma_h) : frame.height;
        const int bytes = av_image_get_linesize(format, frame.width, plane);
        const int depth = depthOfPlane(descriptor, plane);
        for (int y = 0; y < height; ++y)
        {
            std::uint8_t* row = frame.data[plane] + y * frame.linesize[plane];
            if (spec.flatLevels.empty())
            {
                for (int x = 0; x < bytes; ++x)
                    row[x] = storedByte(plane, x, y, index);
                continue;
            }
            const int level = spec.flatLevels[static_cast<std::size_t>(index) % spec.flatLevels.size()]
                                             [static_cast<std::size_t>(plane)];
            if (depth <= 8)
            {
                std::memset(row, level, static_cast<std::size_t>(bytes));
                continue;
            }
            const auto value = static_cast<unsigned>(level) << static_cast<unsigned>(depth - 8);
            for (int x = 0; x + 1 < bytes; x += 2)
            {
                row[x] = static_cast<std::uint8_t>(bigEndian ? value >> 8u : value & 0xffu);
                row[x + 1] = static_cast<std::uint8_t>(bigEndian ? value & 0xffu : value >> 8u);
            }
        }
    }
}
} // namespace

std::uint8_t storedByte(int plane, int x, int y, int frame) noexcept
{
    return static_cast<std::uint8_t>((x * 5 + y * 11 + frame * 40 + plane * 17 + 3) & 0xff);
}

std::uint8_t paletteIndex(int x, int y, int frame) noexcept
{
    return static_cast<std::uint8_t>((x * 7 + y * 3 + frame) & 0xff);
}

std::uint32_t paletteEntry(int index) noexcept
{
    const auto i = static_cast<std::uint32_t>(index);
    return 0xFF000000u | (i << 16u) | ((255u - i) << 8u) | (i / 2u);
}

bool canWriteVideo(const VideoFixtureSpec& spec)
{
    if (av_guess_format(spec.muxer.c_str(), nullptr, nullptr) == nullptr)
        return false;
    const AVCodec* const encoder = avcodec_find_encoder_by_name(spec.encoder.c_str());
    const AVPixelFormat pixelFormat = av_get_pix_fmt(spec.pixelFormat.c_str());
    if (encoder == nullptr || pixelFormat == AV_PIX_FMT_NONE)
        return false;
    const void* supported = nullptr;
    int supportedCount = 0;
    if (avcodec_get_supported_config(nullptr, encoder, AV_CODEC_CONFIG_PIX_FORMAT, 0u, &supported, &supportedCount) < 0)
        return false;
    if (supported == nullptr)
        return true;
    const auto* const formats = static_cast<const AVPixelFormat*>(supported);
    return std::find(formats, formats + supportedCount, pixelFormat) != formats + supportedCount;
}

std::string writeVideo(const std::filesystem::path& path, const VideoFixtureSpec& spec)
{
    const auto text = path.u8string();
    const std::string utf8(reinterpret_cast<const char*>(text.data()), text.size());
    AVFormatContext* raw = nullptr;
    if (const int allocated = avformat_alloc_output_context2(&raw, nullptr, spec.muxer.c_str(), utf8.c_str());
        allocated < 0 || raw == nullptr)
        return describe("muxer", allocated);
    const std::unique_ptr<AVFormatContext, OutputDeleter> output(raw);

    std::vector<Stream> streams;
    for (int index = 0; index <= spec.extraStreams; ++index)
    {
        const bool primary = index == 0;
        const AVCodec* codec = avcodec_find_encoder_by_name(primary ? spec.encoder.c_str() : "rawvideo");
        if (codec == nullptr)
            return "encoder " + (primary ? spec.encoder : std::string("rawvideo")) + " is not in this build";
        const AVPixelFormat format = av_get_pix_fmt(primary ? spec.pixelFormat.c_str() : "gray");
        if (format == AV_PIX_FMT_NONE)
            return "pixel format " + spec.pixelFormat + " is not in this build";
        Stream stream;
        stream.format = format;
        stream.stream = avformat_new_stream(output.get(), nullptr);
        stream.encoder.reset(avcodec_alloc_context3(codec));
        if (stream.stream == nullptr || !stream.encoder)
            return "stream allocation failed";
        AVCodecContext& encoder = *stream.encoder;
        encoder.width = spec.width;
        encoder.height = spec.height;
        encoder.pix_fmt = format;
        encoder.time_base = {1, spec.timeBaseDenominator};
        encoder.framerate = {spec.timeBaseDenominator, 1};
        encoder.gop_size = primary ? spec.gop : 1;
        encoder.max_b_frames = primary ? spec.bframes : 0;
        encoder.thread_count = 1;
        if (primary)
        {
            encoder.color_range = static_cast<AVColorRange>(spec.colorRange);
            encoder.colorspace = static_cast<AVColorSpace>(spec.colorSpace);
            encoder.color_primaries = static_cast<AVColorPrimaries>(spec.colorPrimaries);
            encoder.color_trc = static_cast<AVColorTransferCharacteristic>(spec.colorTransfer);
            encoder.sample_aspect_ratio = {spec.sarNumerator, spec.sarDenominator};
        }
        if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0)
            encoder.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* options = nullptr;
        if (primary && !spec.encoderOptions.empty())
            av_dict_parse_string(&options, spec.encoderOptions.c_str(), "=", ":", 0);
        const int opened = avcodec_open2(&encoder, codec, &options);
        av_dict_free(&options);
        if (opened < 0)
            return describe("encoder", opened);
        if (const int copied = avcodec_parameters_from_context(stream.stream->codecpar, &encoder); copied < 0)
            return describe("parameters", copied);
        stream.stream->time_base = encoder.time_base;
        if (primary)
            stream.stream->sample_aspect_ratio = encoder.sample_aspect_ratio;
        streams.push_back(std::move(stream));
    }

    if ((output->oformat->flags & AVFMT_NOFILE) == 0)
        if (const int opened = avio_open(&output->pb, utf8.c_str(), AVIO_FLAG_WRITE); opened < 0)
            return describe("open", opened);
    if (const int written = avformat_write_header(output.get(), nullptr); written < 0)
        return describe("header", written);

    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::array<std::uint8_t, AVPALETTE_SIZE> palette{};
    const auto drain = [&](Stream& stream, AVFrame* frame, bool primary) -> std::string {
        if (const int sent = avcodec_send_frame(stream.encoder.get(), frame); sent < 0)
            return describe("encode", sent);
        for (;;)
        {
            const int received = avcodec_receive_packet(stream.encoder.get(), packet.get());
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                return {};
            if (received < 0)
                return describe("encode", received);
            if (primary && spec.paletteSideData)
            {
                std::uint8_t* side = av_packet_new_side_data(packet.get(), AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
                if (side == nullptr)
                    return "palette side data allocation failed";
                std::memcpy(side, palette.data(), palette.size());
            }
            av_packet_rescale_ts(packet.get(), stream.encoder->time_base, stream.stream->time_base);
            packet->stream_index = stream.stream->index;
            if (const int muxed = av_interleaved_write_frame(output.get(), packet.get()); muxed < 0)
                return describe("mux", muxed);
        }
    };

    for (int index = 0; index < spec.frames; ++index)
    {
        for (std::size_t s = 0u; s < streams.size(); ++s)
        {
            Stream& stream = streams[s];
            const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
            if (!frame)
                return "frame allocation failed";
            frame->format = stream.format;
            frame->width = spec.width;
            frame->height = spec.height;
            if (const int allocated = av_frame_get_buffer(frame.get(), 0); allocated < 0)
                return describe("frame", allocated);
            if (s == 0u)
            {
                const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(stream.format);
                fillPrimary(*frame, *descriptor, spec, index);
                if ((descriptor->flags & AV_PIX_FMT_FLAG_PAL) != 0)
                    std::memcpy(palette.data(), frame->data[1], palette.size());
            }
            else
            {
                for (int y = 0; y < spec.height; ++y)
                    std::memset(frame->data[0] + y * frame->linesize[0], 200, static_cast<std::size_t>(spec.width));
            }
            frame->pts = index;
            if (auto failure = drain(stream, frame.get(), s == 0u); !failure.empty())
                return failure;
        }
    }
    for (std::size_t s = 0u; s < streams.size(); ++s)
        if (auto failure = drain(streams[s], nullptr, s == 0u); !failure.empty())
            return failure;
    if (const int finished = av_write_trailer(output.get()); finished < 0)
        return describe("trailer", finished);
    return {};
}

void quietFfmpegLog()
{
    av_log_set_level(AV_LOG_QUIET);
}

std::optional<std::string> requiredVideoDevice(const char* variable)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0u;
    const auto copied = _dupenv_s(&value, &length, variable);
    std::optional<std::string> required;
    if (copied == 0 && value != nullptr && *value != '\0')
        required = std::string(value);
    std::free(value);
    return required;
#else
    const char* const value = std::getenv(variable);
    if (value == nullptr || *value == '\0')
        return std::nullopt;
    return std::string(value);
#endif
}

} // namespace an::test
