// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "BenchVideo.h"

#include "Digest.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace an::bench
{
namespace
{
struct OutputDeleter
{
    void operator()(AVFormatContext* context) const noexcept
    {
        if (context == nullptr)
            return;
        if (context->pb != nullptr)
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

std::string describe(const char* what, int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return std::string(what) + ": " + text;
}
} // namespace

std::string writeFfv1Video(const std::filesystem::path& path, std::span<const std::byte> samples, int width, int height,
                           int frames, int slices)
{
    if (width <= 0 || height <= 0 || samples.size() < static_cast<std::size_t>(width) * height * 2u)
        return "the sample span does not hold one frame";
    const auto text = path.u8string();
    const std::string utf8(reinterpret_cast<const char*>(text.data()), text.size());
    AVFormatContext* raw = nullptr;
    if (const int allocated = avformat_alloc_output_context2(&raw, nullptr, "matroska", utf8.c_str());
        allocated < 0 || raw == nullptr)
        return describe("muxer", allocated);
    const std::unique_ptr<AVFormatContext, OutputDeleter> output(raw);
    output->flags |= AVFMT_FLAG_BITEXACT;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (codec == nullptr)
        return "the FFV1 encoder is not in this FFmpeg build";
    AVStream* stream = avformat_new_stream(output.get(), nullptr);
    const std::unique_ptr<AVCodecContext, EncoderDeleter> encoder(avcodec_alloc_context3(codec));
    if (stream == nullptr || !encoder)
        return "stream allocation failed";
    encoder->width = width;
    encoder->height = height;
    encoder->pix_fmt = AV_PIX_FMT_GRAY16LE;
    encoder->time_base = {1, 25};
    encoder->framerate = {25, 1};
    encoder->gop_size = 1;
    encoder->thread_count = 1;
    encoder->flags |= AV_CODEC_FLAG_BITEXACT;
    if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "level", "3", 0);
    av_dict_set_int(&options, "slices", slices, 0);
    const int opened = avcodec_open2(encoder.get(), codec, &options);
    av_dict_free(&options);
    if (opened < 0)
        return describe("encoder", opened);
    if (const int copied = avcodec_parameters_from_context(stream->codecpar, encoder.get()); copied < 0)
        return describe("parameters", copied);
    stream->time_base = encoder->time_base;
    if (const int created = avio_open(&output->pb, utf8.c_str(), AVIO_FLAG_WRITE); created < 0)
        return describe("open", created);
    if (const int written = avformat_write_header(output.get(), nullptr); written < 0)
        return describe("header", written);

    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    const auto drain = [&](AVFrame* frame) -> std::string {
        if (const int sent = avcodec_send_frame(encoder.get(), frame); sent < 0)
            return describe("encode", sent);
        for (;;)
        {
            const int received = avcodec_receive_packet(encoder.get(), packet.get());
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                return {};
            if (received < 0)
                return describe("encode", received);
            av_packet_rescale_ts(packet.get(), encoder->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (const int muxed = av_interleaved_write_frame(output.get(), packet.get()); muxed < 0)
                return describe("mux", muxed);
        }
    };
    for (int index = 0; index < frames; ++index)
    {
        const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
        if (!frame)
            return "frame allocation failed";
        frame->format = AV_PIX_FMT_GRAY16LE;
        frame->width = width;
        frame->height = height;
        if (const int allocated = av_frame_get_buffer(frame.get(), 0); allocated < 0)
            return describe("frame", allocated);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 2u;
        for (int y = 0; y < height; ++y)
            std::memcpy(frame->data[0] + y * frame->linesize[0], samples.data() + y * rowBytes, rowBytes);
        frame->pts = index;
        if (auto failure = drain(frame.get()); !failure.empty())
            return failure;
    }
    if (auto failure = drain(nullptr); !failure.empty())
        return failure;
    if (const int finished = av_write_trailer(output.get()); finished < 0)
        return describe("trailer", finished);
    return {};
}

std::string readAviFrames(const std::filesystem::path& path, std::span<const std::byte> expected, AviFrames& out)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return "the AVI file could not be opened";
    const std::vector<char> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto le32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(file[at])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(file[at + 1u])) << 8u) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(file[at + 2u])) << 16u) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(file[at + 3u])) << 24u);
    };
    const auto fourcc = [&](std::size_t at, const char* code) { return std::memcmp(file.data() + at, code, 4u) == 0; };
    Sha256 digest;
    out = AviFrames{};
    out.everyFrameIs = true;
    std::size_t section = 0u;
    while (section + 12u <= file.size())
    {
        if (!fourcc(section, "RIFF"))
            return "a RIFF section was expected";
        const std::size_t sectionEnd = section + 8u + le32(section + 4u);
        if (sectionEnd > file.size())
            return "a RIFF section runs past the file";
        for (std::size_t chunk = section + 12u; chunk + 8u <= sectionEnd;)
        {
            const std::uint32_t size = le32(chunk + 4u);
            if (chunk + 8u + size > sectionEnd)
                return "a chunk runs past its section";
            if (fourcc(chunk, "LIST") && size >= 4u && fourcc(chunk + 8u, "movi"))
            {
                const std::size_t listEnd = chunk + 8u + size;
                for (std::size_t inner = chunk + 12u; inner + 8u <= listEnd;)
                {
                    const std::uint32_t innerSize = le32(inner + 4u);
                    if (inner + 8u + innerSize > listEnd)
                        return "a movie chunk runs past its list";
                    const bool frame = file[inner + 2u] == 'd' && (file[inner + 3u] == 'b' || file[inner + 3u] == 'c');
                    if (frame)
                    {
                        digest.update(file.data() + inner + 8u, innerSize);
                        ++out.frames;
                        if (innerSize != expected.size() ||
                            std::memcmp(file.data() + inner + 8u, expected.data(), expected.size()) != 0)
                            out.everyFrameIs = false;
                    }
                    inner += 8u + innerSize + (innerSize & 1u);
                }
            }
            chunk += 8u + size + (size & 1u);
        }
        section = sectionEnd;
    }
    out.digest = digest.hex();
    return {};
}

std::string readVideoPackets(const std::filesystem::path& path, std::span<const std::byte> grey, int width, int height,
                             VideoPackets& out)
{
    out = VideoPackets{};
    const auto text = path.u8string();
    const std::string utf8(reinterpret_cast<const char*>(text.data()), text.size());
    AVFormatContext* raw = nullptr;
    if (const int opened = avformat_open_input(&raw, utf8.c_str(), nullptr, nullptr); opened < 0)
        return describe("open", opened);
    const auto closeInput = [](AVFormatContext* context) { avformat_close_input(&context); };
    const std::unique_ptr<AVFormatContext, decltype(closeInput)> input(raw, closeInput);
    if (const int found = avformat_find_stream_info(input.get(), nullptr); found < 0)
        return describe("stream info", found);
    const int stream = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0)
        return describe("video stream", stream);
    const AVCodecParameters& parameters = *input->streams[stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters.codec_id);
    if (codec == nullptr)
        return "no decoder for the stream";
    out.codec = codec->name;
    const std::unique_ptr<AVCodecContext, EncoderDeleter> decoder(avcodec_alloc_context3(codec));
    if (!decoder || avcodec_parameters_to_context(decoder.get(), &parameters) < 0)
        return "the decoder could not be prepared";
    decoder->thread_count = 1;
    if (const int opened = avcodec_open2(decoder.get(), codec, nullptr); opened < 0)
        return describe("decoder", opened);
    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (!packet || !frame)
        return "allocation failed";
    Sha256 digest;
    double squared = 0.0;
    std::uint64_t samples = 0u;
    const auto take = [&]() -> std::string {
        for (;;)
        {
            const int received = avcodec_receive_frame(decoder.get(), frame.get());
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
                return {};
            if (received < 0)
                return describe("decode", received);
            if (frame->width != width || frame->height != height)
                return "a decoded frame has another size";
            ++out.frames;
            for (int y = 0; y < height; ++y)
            {
                const std::uint8_t* row = frame->data[0] + y * frame->linesize[0];
                for (int x = 0; x < width; ++x)
                {
                    const double value = std::to_integer<int>(grey[static_cast<std::size_t>(y) * width + x]);
                    const double error = row[x] - std::round(16.0 + 219.0 * value / 255.0);
                    squared += error * error;
                    ++samples;
                }
            }
        }
    };
    for (;;)
    {
        const int read = av_read_frame(input.get(), packet.get());
        if (read == AVERROR_EOF)
            break;
        if (read < 0)
            return describe("read", read);
        if (packet->stream_index == stream)
        {
            digest.update(packet->data, static_cast<std::size_t>(packet->size));
            ++out.packets;
            if (const int sent = avcodec_send_packet(decoder.get(), packet.get()); sent < 0)
                return describe("decode", sent);
            av_packet_unref(packet.get());
            if (auto failure = take(); !failure.empty())
                return failure;
        }
        else
            av_packet_unref(packet.get());
    }
    if (const int sent = avcodec_send_packet(decoder.get(), nullptr); sent < 0)
        return describe("drain", sent);
    if (auto failure = take(); !failure.empty())
        return failure;
    out.digest = digest.hex();
    out.lumaRms = samples == 0u ? 0.0 : std::sqrt(squared / static_cast<double>(samples));
    return {};
}
} // namespace an::bench
