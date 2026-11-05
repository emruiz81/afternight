// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - planning an encoded video stream, opening its encoder and filling its pictures.
//
// The tables here - the constant rate factor at each quality level, the encoders each coding tries and
// their order, the VideoToolbox quality scale - are held as immutable data. An
// encoder that leaves one of its settings untaken is never run on its own defaults instead: for the
// software encoder that is a defect of this adapter, and for a device encoder it is a reason to try
// the next candidate. Pictures are 4:2:0. A grey frame is written into the luma plane by an exact
// table with both chroma planes neutral, and a colour frame goes through the scaler under the stated
// matrix and range.

#include "io/sequences/ffmpeg/FfmpegEncoding.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

namespace an::ffmpeg_detail
{
namespace
{
constexpr std::uint32_t kLargestRational = static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());

// The constant rate factor at each quality level, from `Draft` to `Best`, per coding.
constexpr std::array<int, 5u> kH264Factors{28, 26, 24, 22, 20};
constexpr std::array<int, 5u> kH265Factors{32, 29, 26, 23, 20};
constexpr std::array<int, 5u> kVp9Factors{35, 32, 28, 24, 20};

// libvpx-vp9's constant rate factor range.
constexpr int kVp9LowestFactor = 0;
constexpr int kVp9HighestFactor = 63;

// The limited-range luma code of every eight-bit sample, round(16 + 219v/255) in integer arithmetic. No sample
// falls halfway between two codes, so the direction a tie would round never arises.
constexpr std::array<std::uint8_t, 256u> kLimitedLumaCodes = [] {
    std::array<std::uint8_t, 256u> codes{};
    for (std::uint32_t sample = 0u; sample < 256u; ++sample)
        codes[sample] = static_cast<std::uint8_t>((8415u + 438u * sample) / 510u);
    return codes;
}();

// The code both chroma planes of a grey picture hold.
constexpr int kNeutralChroma = 128;

Status unsupported(const char* message, const char* parameter)
{
    return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, message, parameter, {});
}

Status descriptorArgument(const char* message, const char* parameter)
{
    return videoFailure(ErrorCode::InvalidArgument, SequenceFailure::InvalidDescriptor, message, parameter, {});
}

const char* muxerNameOf(FfmpegVideoContainer container) noexcept
{
    switch (container)
    {
    case FfmpegVideoContainer::Mp4:
        return "mp4";
    case FfmpegVideoContainer::Matroska:
        return "matroska";
    case FfmpegVideoContainer::WebM:
        return "webm";
    }
    return nullptr;
}

bool setOption(AVDictionary*& options, const char* key, const std::string& value) noexcept
{
    return av_dict_set(&options, key, value.c_str(), 0) >= 0;
}

// The encoders each coding tries, in order, each list ending with the software encoder a request that
// prefers the processor uses alone.
constexpr std::array<EncoderCandidate, 6u> kH264Candidates{{{"h264_nvenc", EncoderFamily::Nvenc},
                                                            {"h264_qsv", EncoderFamily::QuickSync},
                                                            {"h264_amf", EncoderFamily::Amf},
                                                            {"h264_vaapi", EncoderFamily::Vaapi},
                                                            {"h264_videotoolbox", EncoderFamily::VideoToolbox},
                                                            {"libx264", EncoderFamily::Software}}};
constexpr std::array<EncoderCandidate, 6u> kH265Candidates{{{"hevc_nvenc", EncoderFamily::Nvenc},
                                                            {"hevc_qsv", EncoderFamily::QuickSync},
                                                            {"hevc_amf", EncoderFamily::Amf},
                                                            {"hevc_vaapi", EncoderFamily::Vaapi},
                                                            {"hevc_videotoolbox", EncoderFamily::VideoToolbox},
                                                            {"libx265", EncoderFamily::Software}}};
constexpr std::array<EncoderCandidate, 2u> kVp9Candidates{
    {{"vp9_vaapi", EncoderFamily::Vaapi}, {"libvpx-vp9", EncoderFamily::Software}}};

// VideoToolbox's quality at each level: (10 * level + 15) lambda units, level zero being `Draft`.
constexpr int kVideoToolboxQualityStep = 10;
constexpr int kVideoToolboxQualityBase = 15;

/// Frees a dictionary on every way out of a scope.
struct DictionaryOwner
{
    AVDictionary* entries = nullptr;
    ~DictionaryOwner()
    {
        av_dict_free(&entries);
    }
};
} // namespace

std::span<const EncoderCandidate> encoderCandidatesOf(FfmpegVideoCodec codec,
                                                      FfmpegEncoderPreference preference) noexcept
{
    const bool devices = preference == FfmpegEncoderPreference::DeviceFirst;
    switch (codec)
    {
    case FfmpegVideoCodec::H264:
        return devices ? std::span<const EncoderCandidate>(kH264Candidates)
                       : std::span<const EncoderCandidate>(kH264Candidates).last(1u);
    case FfmpegVideoCodec::H265:
        return devices ? std::span<const EncoderCandidate>(kH265Candidates)
                       : std::span<const EncoderCandidate>(kH265Candidates).last(1u);
    case FfmpegVideoCodec::Vp9:
        return devices ? std::span<const EncoderCandidate>(kVp9Candidates)
                       : std::span<const EncoderCandidate>(kVp9Candidates).last(1u);
    }
    return {};
}

const char* encoderNameOf(FfmpegVideoCodec codec) noexcept
{
    switch (codec)
    {
    case FfmpegVideoCodec::H264:
        return "libx264";
    case FfmpegVideoCodec::H265:
        return "libx265";
    case FfmpegVideoCodec::Vp9:
        return "libvpx-vp9";
    }
    return nullptr;
}

int constantRateFactorOf(FfmpegVideoCodec codec, FfmpegEncodeQuality quality, std::uint32_t width,
                         std::uint32_t height) noexcept
{
    const auto level = static_cast<std::size_t>(quality);
    switch (codec)
    {
    case FfmpegVideoCodec::H264:
        return kH264Factors[level];
    case FfmpegVideoCodec::H265:
        return kH265Factors[level];
    case FfmpegVideoCodec::Vp9: {
        // VP9's factor follows the picture's area: five times the natural logarithm of its ratio to 1920x1080,
        // at least minus four, subtracted in single precision and truncated toward zero.
        const auto area = static_cast<float>(std::uint64_t{width} * height);
        float sizeFactor = 5.0f * std::log(area / (1920.0f * 1080.0f));
        if (sizeFactor < -4.0f)
            sizeFactor = -4.0f;
        const auto factor = static_cast<int>(static_cast<double>(kVp9Factors[level]) - static_cast<double>(sizeFactor));
        return factor < kVp9LowestFactor ? kVp9LowestFactor : factor > kVp9HighestFactor ? kVp9HighestFactor : factor;
    }
    }
    return 0;
}

Result<EncodePlan> planEncode(const SequenceDescriptor& descriptor, const FfmpegSinkRequest& request)
{
    if (static_cast<std::uint8_t>(request.codec) > static_cast<std::uint8_t>(FfmpegVideoCodec::Vp9))
        return argumentFailure("the video coding is not one this build knows", "codec");
    if (static_cast<std::uint8_t>(request.container) > static_cast<std::uint8_t>(FfmpegVideoContainer::WebM))
        return argumentFailure("the container is not one this build knows", "container");
    if (static_cast<std::uint8_t>(request.quality) > static_cast<std::uint8_t>(FfmpegEncodeQuality::Best))
        return argumentFailure("the quality level is not one this build knows", "quality");
    if (static_cast<std::uint8_t>(request.matrix) > static_cast<std::uint8_t>(FfmpegEncodeMatrix::Bt601))
        return argumentFailure("the matrix is not one this build knows", "matrix");
    if (static_cast<std::uint8_t>(request.range) > static_cast<std::uint8_t>(FfmpegEncodeRange::Full))
        return argumentFailure("the code range is not one this build knows", "range");
    if (static_cast<std::uint8_t>(request.mosaicFrames) >
        static_cast<std::uint8_t>(FfmpegMosaicPolicy::WriteSamplesAsMono))
        return argumentFailure("the mosaic policy is not one this build knows", "mosaicFrames");
    if (request.encoderThreads == 0u)
        return argumentFailure("an encoder runs on at least one thread", "encoderThreads");
    if (static_cast<std::uint8_t>(request.encoderPreference) >
        static_cast<std::uint8_t>(FfmpegEncoderPreference::DeviceFirst))
        return argumentFailure("the encoder preference is not one this build knows", "encoderPreference");

    const SequenceFormat expectedFormat =
        request.container == FfmpegVideoContainer::Mp4 ? SequenceFormat::Mp4 : SequenceFormat::Video;
    if (descriptor.format != expectedFormat)
        return descriptorArgument("an MP4 container is described as MP4 and Matroska and WebM as video", "format");
    if (!descriptor.fixedFrame)
        return descriptorArgument("a video stream stores one frame description for every frame", "fixedFrame");
    if (descriptor.seekByIndex)
        return descriptorArgument("a sink cannot seek", "seekByIndex");
    if (descriptor.seekByTime)
        return descriptorArgument("a sink cannot seek", "seekByTime");
    if (const Status checked = validateSequenceDescriptor(descriptor); checked.failed())
        return checked;
    if (!descriptor.nominalFrameRate)
        return descriptorArgument("a video stream presents frames at a stated rate", "nominalFrameRate");
    if (descriptor.timestampsRequired)
        return unsupported("a stream written at a constant rate stores no time for each frame", "timestampsRequired");
    if (request.container == FfmpegVideoContainer::WebM && request.codec != FfmpegVideoCodec::Vp9)
        return unsupported("a WebM container holds VP9 alone of these codings", "codec");
    const SequenceRate rate = *descriptor.nominalFrameRate;
    if (rate.numerator > kLargestRational || rate.denominator > kLargestRational)
        return videoFailure(ErrorCode::LimitExceeded, SequenceFailure::UnsupportedEncoding,
                            "FFmpeg states a frame rate in 32-bit signed terms", "nominalFrameRate", {});

    // These muxers store one-millisecond timestamps, irrespective of the requested stream time base.
    // A shorter period aliases distinct frames, which cannot then be indexed or sought unambiguously.
    if (request.container != FfmpegVideoContainer::Mp4 &&
        std::uint64_t{rate.numerator} > std::uint64_t{rate.denominator} * 1000u)
        return unsupported("Matroska and WebM cannot distinguish frames faster than 1000 per second",
                           "nominalFrameRate");

    const RawFrameDescriptor& frame = *descriptor.fixedFrame;
    const RawSampleFormat format = frame.sampleFormat();
    if (format.sampleClass != RawSampleClass::UnsignedInteger || format.sizeBytes != 1u)
        return unsupported("an eight-bit video stream is encoded from one unsigned byte per sample", "sampleFormat");
    if (frame.channelSchema() != ChannelSchema::Mono && frame.channelSchema() != ChannelSchema::Rgb)
        return unsupported("a video stream is encoded from grey and RGB frames", "channelSchema");
    if (frame.isMosaiced() && request.mosaicFrames == FfmpegMosaicPolicy::Refuse)
        return unsupported("a video stream cannot state a filter array", "cfa");
    if (frame.pixelAspectRatio() != 1.0)
        return unsupported("a video stream is written with square pixels", "pixelAspectRatio");
    const std::uint32_t width = frame.extent().width();
    const std::uint32_t height = frame.extent().height();
    if (width % 2u != 0u || height % 2u != 0u)
        return unsupported("a 4:2:0 stream has an even width and height", "extent");
    if (width > kLargestRational || height > kLargestRational)
        return unsupported("FFmpeg states a picture's size in 32-bit signed terms", "extent");

    const char* encoderName = encoderNameOf(request.codec);
    bool present = false;
    for (const EncoderCandidate& candidate : encoderCandidatesOf(request.codec, request.encoderPreference))
        present = present || avcodec_find_encoder_by_name(candidate.name) != nullptr;
    if (!present)
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            std::string("this build's FFmpeg has no ") + encoderName + " encoder", "codec", {});

    EncodePlan plan;
    plan.codec = request.codec;
    plan.container = request.container;
    plan.encoderName = encoderName;
    plan.muxerName = muxerNameOf(request.container);
    plan.width = static_cast<int>(width);
    plan.height = static_cast<int>(height);
    plan.grey = frame.channelSchema() == ChannelSchema::Mono;
    plan.rowsArriveBottomUp = frame.coordinateOrigin() == CoordinateOrigin::BottomLeft;
    plan.frameRate = AVRational{static_cast<int>(rate.numerator), static_cast<int>(rate.denominator)};
    plan.timeBase = AVRational{static_cast<int>(rate.denominator), static_cast<int>(rate.numerator)};
    plan.constantRateFactor = constantRateFactorOf(request.codec, request.quality, width, height);
    plan.quality = request.quality;
    plan.matrix = request.matrix;
    plan.range = request.range;
    plan.encoderThreads = request.encoderThreads;
    plan.preference = request.encoderPreference;
    return plan;
}

Result<OpenedEncoder> openEncoder(const EncodePlan& plan, const EncoderCandidate& candidate, bool globalHeader,
                                  std::string_view path)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(candidate.name);
    if (codec == nullptr)
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            std::string("this build's FFmpeg has no ") + candidate.name + " encoder", "codec", path);
    const bool software = candidate.family == EncoderFamily::Software;
    OpenedEncoder opened;
    opened.context.reset(avcodec_alloc_context3(codec));
    if (!opened.context)
        return Status::failure(ErrorCode::AllocationFailed);
    const CodecContextPtr& encoder = opened.context;
    encoder->width = plan.width;
    encoder->height = plan.height;
    encoder->pix_fmt = software ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
    encoder->time_base = plan.timeBase;
    encoder->framerate = plan.frameRate;
    encoder->gop_size = kKeyframeInterval;
    encoder->bit_rate = 0;
    if (software)
        encoder->thread_count = static_cast<int>(plan.encoderThreads);
    encoder->color_range = plan.range == FfmpegEncodeRange::Full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    // A grey picture's chroma is neutral under every matrix, so no matrix was applied and none is stated.
    encoder->colorspace = plan.grey                                  ? AVCOL_SPC_UNSPECIFIED
                          : plan.matrix == FfmpegEncodeMatrix::Bt709 ? AVCOL_SPC_BT709
                                                                     : AVCOL_SPC_SMPTE170M;
    if (globalHeader)
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (candidate.family == EncoderFamily::Vaapi)
    {
        AVBufferRef* device = nullptr;
        if (const int created = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
            created < 0)
            return encoderFailure(created, "the VA-API device could not be opened", "codec", path);
        opened.device.reset(device);
        opened.frames.reset(av_hwframe_ctx_alloc(opened.device.get()));
        if (!opened.frames)
            return Status::failure(ErrorCode::AllocationFailed);
        auto* const surfaces = reinterpret_cast<AVHWFramesContext*>(opened.frames->data);
        surfaces->format = AV_PIX_FMT_VAAPI;
        surfaces->sw_format = AV_PIX_FMT_NV12;
        surfaces->width = plan.width;
        surfaces->height = plan.height;
        if (const int initialised = av_hwframe_ctx_init(opened.frames.get()); initialised < 0)
            return encoderFailure(initialised, "the VA-API surfaces could not be created", "codec", path);
        encoder->pix_fmt = AV_PIX_FMT_VAAPI;
        encoder->hw_frames_ctx = av_buffer_ref(opened.frames.get());
        if (encoder->hw_frames_ctx == nullptr)
            return Status::failure(ErrorCode::AllocationFailed);
    }

    DictionaryOwner options;
    const std::string factor = std::to_string(plan.constantRateFactor);
    bool set = true;
    switch (candidate.family)
    {
    case EncoderFamily::Software:
        set = setOption(options.entries, "crf", factor);
        switch (plan.codec)
        {
        case FfmpegVideoCodec::H264:
            set = set && setOption(options.entries, "preset", "medium") && setOption(options.entries, "tune", "grain");
            break;
        case FfmpegVideoCodec::H265: {
            // Without a stated pool, libx265 starts one thread per logical processor whatever the thread count.
            const std::string pools =
                plan.encoderThreads == 1u ? std::string("none") : std::to_string(plan.encoderThreads);
            set = set && setOption(options.entries, "preset", "medium") &&
                  setOption(options.entries, "tune", "grain") &&
                  setOption(options.entries, "x265-params", "pools=" + pools + ":frame-threads=1");
            break;
        }
        case FfmpegVideoCodec::Vp9:
            set = set && setOption(options.entries, "speed", "1") &&
                  setOption(options.entries, "frame-parallel", "1") && setOption(options.entries, "lag-in-frames", "1");
            break;
        }
        break;
    case EncoderFamily::Nvenc:
        set = setOption(options.entries, "preset", "p4") && setOption(options.entries, "rc", "constqp") &&
              setOption(options.entries, "qp", factor);
        break;
    case EncoderFamily::QuickSync:
        set = setOption(options.entries, "preset", "medium");
        encoder->global_quality = plan.constantRateFactor;
        break;
    case EncoderFamily::Amf:
        set = setOption(options.entries, "rc", "cqp") && setOption(options.entries, "qp_i", factor) &&
              setOption(options.entries, "qp_p", factor) && setOption(options.entries, "qp_b", factor);
        break;
    case EncoderFamily::Vaapi:
        set = setOption(options.entries, "rc_mode", "CQP");
        encoder->global_quality = plan.constantRateFactor;
        break;
    case EncoderFamily::VideoToolbox:
        encoder->flags |= AV_CODEC_FLAG_QSCALE;
        encoder->global_quality =
            (kVideoToolboxQualityStep * static_cast<int>(plan.quality) + kVideoToolboxQualityBase) * FF_QP2LAMBDA;
        break;
    }
    if (!set)
        return Status::failure(ErrorCode::AllocationFailed);
    const int opening = avcodec_open2(encoder.get(), codec, &options.entries);
    const bool unconsumed = av_dict_count(options.entries) != 0;
    if (opening < 0)
        return encoderFailure(opening, "the encoder could not be opened", "codec", path);
    if (unconsumed && software)
        return Status::failure(ErrorCode::Internal,
                               {.message = "the encoder did not take every setting the adapter gave it",
                                .operation = "ffmpeg",
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "codec")}});
    // A device encoder in another FFmpeg build may know its settings by other names; it is passed over rather than
    // run with settings it ignored.
    if (unconsumed)
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            std::string(candidate.name) + " did not take every setting the adapter gave it", "codec",
                            path);
    return opened;
}

void rearrangeToNv12(const AVFrame& planar, AVFrame& semiPlanar) noexcept
{
    const int width = planar.width;
    const int height = planar.height;
    for (int row = 0; row < height; ++row)
        std::memcpy(semiPlanar.data[0] + static_cast<std::ptrdiff_t>(row) * semiPlanar.linesize[0],
                    planar.data[0] + static_cast<std::ptrdiff_t>(row) * planar.linesize[0],
                    static_cast<std::size_t>(width));
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    for (int row = 0; row < chromaHeight; ++row)
    {
        const std::uint8_t* const blue = planar.data[1] + static_cast<std::ptrdiff_t>(row) * planar.linesize[1];
        const std::uint8_t* const red = planar.data[2] + static_cast<std::ptrdiff_t>(row) * planar.linesize[2];
        std::uint8_t* const pair = semiPlanar.data[1] + static_cast<std::ptrdiff_t>(row) * semiPlanar.linesize[1];
        for (int column = 0; column < chromaWidth; ++column)
        {
            pair[2 * column] = blue[column];
            pair[2 * column + 1] = red[column];
        }
    }
}

AVPixelFormat sourceFormatOf(const ImageLayout& layout) noexcept
{
    return layout.planeCount() == 3u ? AV_PIX_FMT_GBRP : AV_PIX_FMT_RGB24;
}

Result<ScaleContextPtr> openConverter(const EncodePlan& plan, AVPixelFormat source)
{
    ScaleContextPtr converter(
        sws_getContext(plan.width, plan.height, source, plan.width, plan.height, AV_PIX_FMT_YUV420P,
                       SWS_BILINEAR | SWS_ACCURATE_RND | SWS_BITEXACT | SWS_FULL_CHR_H_INP, nullptr, nullptr, nullptr));
    if (!converter)
        return Status::failure(ErrorCode::AllocationFailed);
    const int* coefficients =
        sws_getCoefficients(plan.matrix == FfmpegEncodeMatrix::Bt709 ? SWS_CS_ITU709 : SWS_CS_ITU601);
    // The frame's samples use every code; the picture's range is the plan's.
    if (sws_setColorspaceDetails(converter.get(), coefficients, 1, coefficients,
                                 plan.range == FfmpegEncodeRange::Full ? 1 : 0, 0, 1 << 16, 1 << 16) < 0)
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            "the scaler cannot apply the stated matrix and range", "matrix", {});
    return converter;
}

Status convertFrame(SwsContext& converter, const EncodePlan& plan, const RawFrame& frame, const ImageLayout& layout,
                    AVFrame& picture, std::string_view path)
{
    // A planar frame is handed over as green, blue, red planes, the order FFmpeg's planar RGB format uses.
    constexpr std::array<std::uint32_t, 3u> kPlanarOrder{1u, 2u, 0u};
    const std::uint32_t planes = layout.planeCount();
    const int stride = static_cast<int>(layout.rowStrideBytes());
    const std::uint32_t firstRow = plan.rowsArriveBottomUp ? static_cast<std::uint32_t>(plan.height - 1) : 0u;
    std::array<const std::uint8_t*, 4u> source{};
    std::array<int, 4u> strides{};
    for (std::uint32_t index = 0u; index < planes; ++index)
    {
        const std::uint32_t plane = planes == 3u ? kPlanarOrder[index] : index;
        source[index] = reinterpret_cast<const std::uint8_t*>(frame.row(plane, firstRow)->data());
        strides[index] = plan.rowsArriveBottomUp ? -stride : stride;
    }
    const int scaled =
        sws_scale(&converter, source.data(), strides.data(), 0, plan.height, picture.data, picture.linesize);
    if (scaled < 0)
        return encoderFailure(scaled, "the frame could not be converted", "layout", path);
    return Status::success();
}

void convertGreyFrame(const EncodePlan& plan, const RawFrame& frame, AVFrame& picture) noexcept
{
    const auto width = static_cast<std::size_t>(plan.width);
    const bool full = plan.range == FfmpegEncodeRange::Full;
    for (int row = 0; row < plan.height; ++row)
    {
        const auto stored = static_cast<std::uint32_t>(plan.rowsArriveBottomUp ? plan.height - 1 - row : row);
        const auto* from = reinterpret_cast<const std::uint8_t*>(frame.row(0u, stored)->data());
        std::uint8_t* const to = picture.data[0] + static_cast<std::ptrdiff_t>(row) * picture.linesize[0];
        if (full)
        {
            std::memcpy(to, from, width);
            continue;
        }
        for (std::size_t column = 0u; column < width; ++column)
            to[column] = kLimitedLumaCodes[from[column]];
    }
    const auto chromaWidth = static_cast<std::size_t>((plan.width + 1) / 2);
    for (int plane = 1; plane < 3; ++plane)
        for (int row = 0; row < (plan.height + 1) / 2; ++row)
            std::memset(picture.data[plane] + static_cast<std::ptrdiff_t>(row) * picture.linesize[plane],
                        kNeutralChroma, chromaWidth);
}

} // namespace an::ffmpeg_detail
