// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg video source: an indexed stream decoded on demand, by index or by time.
//
// Opening prepares everything a read needs: the container and its index, the delivery plan, and a
// decoder. The processor's decoder is opened first, and a device's replaces it only after the device
// has decoded a real frame of the stream through a second reader of the file, so a device that opens
// but cannot decode this stream never serves a read. Only FFV1 runs its slices on the operation's
// admitted threads, because its slices are independent by construction; every decoder is opened with
// one thread of FFmpeg's own.
//
// A read decodes forward when the requested frame is still ahead of the run in progress, and otherwise
// repositions: it asks the demuxer for the last position at or before the frame's keyframe, recognises
// where the reader actually landed by the packet's timestamp, and reopens the container from its first
// byte when the seek failed or landed past the keyframe. Every packet sent is checked against the
// index, and decoded frames are matched to the index by presentation time, so a container that differs
// from the one indexed fails the read rather than delivering the wrong picture.

#include <AfterNightEngine/IO/FfmpegSequence.h>

#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/sequences/SequenceInternal.h"
#include "io/sequences/ffmpeg/FfmpegDelivery.h"
#include "io/sequences/ffmpeg/FfmpegInput.h"
#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::ffmpeg_detail
{
namespace
{

/// Where the decoder's slice callback finds the operation that is reading, and what stopped it.
struct DecoderBinding
{
    const ExecutionContext* execution = nullptr; ///< The reading operation, or null outside a read.
    Status sliceFailure; ///< What stopped a slice run during the current read, or success.

    /// The pixel format a device decodes into, or none for the processor.
    AVPixelFormat devicePixelFormat = AV_PIX_FMT_NONE;
};

/// Makes an operation the one whose threads run the decoder's slices, for the life of a scope.
class BoundDecoder
{
public:
    BoundDecoder(DecoderBinding& binding, const ExecutionContext& execution) noexcept : m_binding(binding)
    {
        m_binding.execution = &execution;
        m_binding.sliceFailure = Status::success();
    }
    ~BoundDecoder()
    {
        m_binding.execution = nullptr;
    }
    BoundDecoder(const BoundDecoder&) = delete;
    BoundDecoder& operator=(const BoundDecoder&) = delete;

private:
    DecoderBinding& m_binding;
};

/// Runs a decoder's independent slices on the reading operation's admitted threads. The decoder asks
/// for this through its `execute` hook, which FFmpeg otherwise runs serially; with one decoder thread,
/// FFmpeg starts no threads of its own. A stop that interrupts the slices is recorded, and the read
/// that was decoding fails with it instead of delivering a partly decoded frame.
int runSlices(AVCodecContext* context, int (*function)(AVCodecContext*, void*), void* argument, int* results, int count,
              int size) noexcept
{
    auto* binding = static_cast<DecoderBinding*>(context->opaque);
    const auto runRange = [&](std::uint64_t begin, std::uint64_t end) {
        for (std::uint64_t task = begin; task < end; ++task)
        {
            const int value =
                function(context, static_cast<char*>(argument) + static_cast<std::ptrdiff_t>(task) * size);
            if (results != nullptr)
                results[task] = value;
        }
    };
    if (count <= 0)
        return 0;
    const ExecutionContext* execution = binding != nullptr ? binding->execution : nullptr;
    if (execution == nullptr)
    {
        runRange(0u, static_cast<std::uint64_t>(count));
        return 0;
    }
    execution->reportTrace(TraceEventKind::Counter, "ffmpeg", "slice-tasks", static_cast<double>(count), "tasks");
    try
    {
        const Status scheduled =
            parallelFor(*execution, static_cast<std::uint64_t>(count),
                        TaskCost{.bytesPerItem = kMinimumTaskBytes, .operationsPerItem = kMinimumTaskOperations},
                        [&](TaskRange range) -> Status {
                            runRange(range.begin, range.end);
                            return Status::success();
                        });
        if (scheduled.failed() && binding->sliceFailure.ok())
            binding->sliceFailure = scheduled;
    }
    catch (...)
    {
        binding->sliceFailure = Status::failure(ErrorCode::Internal);
    }
    return 0;
}

/// Chooses a device's pixel format when the decoder offers it. A decoder that no longer offers it fails the frame
/// rather than quietly decoding it on the processor, because the device was chosen for the whole stream.
AVPixelFormat chooseDeviceFormat(AVCodecContext* context, const AVPixelFormat* offered) noexcept
{
    const auto* binding = static_cast<const DecoderBinding*>(context->opaque);
    for (const AVPixelFormat* format = offered; *format != AV_PIX_FMT_NONE; ++format)
        if (binding != nullptr && *format == binding->devicePixelFormat)
            return *format;
    return AV_PIX_FMT_NONE;
}

/// A decode device and FFmpeg's name for its type. The first four are the order `Automatic` tries.
struct DeviceChoice
{
    FfmpegDecodeDevice device;
    const char* type;
};

constexpr std::array<DeviceChoice, 5u> kDeviceChoices{{{FfmpegDecodeDevice::Cuda, "cuda"},
                                                       {FfmpegDecodeDevice::D3d11va, "d3d11va"},
                                                       {FfmpegDecodeDevice::Vaapi, "vaapi"},
                                                       {FfmpegDecodeDevice::VideoToolbox, "videotoolbox"},
                                                       {FfmpegDecodeDevice::Vulkan, "vulkan"}}};

/// How a device's semi-planar frame is rearranged into the planar format the processor's decoder produces: the two
/// formats, whether the chroma pair stores red first, the bytes in a sample and how far a sample stored in the
/// high bits is shifted down.
struct Rearrangement
{
    AVPixelFormat device;
    AVPixelFormat planar;
    bool redFirst;
    int sampleBytes;
    int shift;
};

constexpr std::array<Rearrangement, 5u> kRearrangements{{{AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, false, 1, 0},
                                                         {AV_PIX_FMT_NV21, AV_PIX_FMT_YUV420P, true, 1, 0},
                                                         {AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV420P10LE, false, 2, 6},
                                                         {AV_PIX_FMT_P012LE, AV_PIX_FMT_YUV420P12LE, false, 2, 4},
                                                         {AV_PIX_FMT_P016LE, AV_PIX_FMT_YUV420P16LE, false, 2, 0}}};

/// The rearrangement from a device's format to a stored one, or null when none reaches it.
const Rearrangement* rearrangementOf(int device, AVPixelFormat stored) noexcept
{
    for (const Rearrangement& candidate : kRearrangements)
        if (candidate.device == device && candidate.planar == stored)
            return &candidate;
    return nullptr;
}

/// Whether a frame a device transferred can be delivered as the stream's stored format.
bool deliverable(const AVFrame& transferred, AVPixelFormat stored) noexcept
{
    return transferred.format == stored || rearrangementOf(transferred.format, stored) != nullptr;
}

/// Moves a semi-planar frame's samples into a planar frame of the same size without changing one: the luma plane
/// sample by sample, the chroma pair split into its two planes, and each sample stored in the high bits shifted
/// down to the low ones the planar format keeps it in.
void rearrange(const AVFrame& from, AVFrame& to, const Rearrangement& how) noexcept
{
    const int width = from.width;
    const int height = from.height;
    const auto sample = [&](const std::uint8_t* row, int column) noexcept -> std::uint32_t {
        if (how.sampleBytes == 1)
            return row[column];
        return static_cast<std::uint32_t>(row[2 * column]) | (static_cast<std::uint32_t>(row[2 * column + 1]) << 8u);
    };
    const auto store = [&](std::uint8_t* row, int column, std::uint32_t value) noexcept {
        if (how.sampleBytes == 1)
        {
            row[column] = static_cast<std::uint8_t>(value);
            return;
        }
        row[2 * column] = static_cast<std::uint8_t>(value & 0xffu);
        row[2 * column + 1] = static_cast<std::uint8_t>(value >> 8u);
    };
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t* luma = from.data[0] + static_cast<std::ptrdiff_t>(y) * from.linesize[0];
        std::uint8_t* const target = to.data[0] + static_cast<std::ptrdiff_t>(y) * to.linesize[0];
        for (int x = 0; x < width; ++x)
            store(target, x, sample(luma, x) >> static_cast<unsigned>(how.shift));
    }
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    const int blue = how.redFirst ? 2 : 1;
    const int red = how.redFirst ? 1 : 2;
    for (int y = 0; y < chromaHeight; ++y)
    {
        const std::uint8_t* pair = from.data[1] + static_cast<std::ptrdiff_t>(y) * from.linesize[1];
        std::uint8_t* const first = to.data[blue] + static_cast<std::ptrdiff_t>(y) * to.linesize[blue];
        std::uint8_t* const second = to.data[red] + static_cast<std::ptrdiff_t>(y) * to.linesize[red];
        for (int x = 0; x < chromaWidth; ++x)
        {
            store(first, x, sample(pair, 2 * x) >> static_cast<unsigned>(how.shift));
            store(second, x, sample(pair, 2 * x + 1) >> static_cast<unsigned>(how.shift));
        }
    }
}

Status checkOptions(const FfmpegSourceOptions& options)
{
    if (static_cast<std::uint8_t>(options.frameMemory) > static_cast<std::uint8_t>(FfmpegFrameMemory::DecoderOwned))
        return argumentFailure("the frame memory is not one this build knows", "frameMemory");
    if (static_cast<std::uint8_t>(options.sampleDelivery) >
        static_cast<std::uint8_t>(FfmpegSampleDelivery::AsStoredOnly))
        return argumentFailure("the sample delivery is not one this build knows", "sampleDelivery");
    if (static_cast<std::uint8_t>(options.unspecifiedMatrix) > static_cast<std::uint8_t>(FfmpegMatrixAssumption::Bt709))
        return argumentFailure("the matrix assumption is not one this build knows", "unspecifiedMatrix");
    if (static_cast<std::uint8_t>(options.unspecifiedRange) > static_cast<std::uint8_t>(FfmpegRangeAssumption::Full))
        return argumentFailure("the range assumption is not one this build knows", "unspecifiedRange");
    if (static_cast<std::uint8_t>(options.emptyPalette) >
        static_cast<std::uint8_t>(FfmpegEmptyPalettePolicy::Grayscale))
        return argumentFailure("the empty-palette policy is not one this build knows", "emptyPalette");
    if (!isColorSpace(options.colorSpace))
        return argumentFailure("the colour space is not one this build knows", "colorSpace");
    if (!isNumericDomain(options.numericDomain))
        return argumentFailure("the numeric domain is not one this build knows", "numericDomain");
    if (static_cast<std::uint8_t>(options.decodeDevice) > static_cast<std::uint8_t>(FfmpegDecodeDevice::Vulkan))
        return argumentFailure("the decode device is not one this build knows", "decodeDevice");
    return Status::success();
}

/// An opened, indexed, planned video with its decoder open. The decoder is declared after its binding so
/// it is released first.
struct PreparedVideo
{
    OpenedContainer container;
    std::unique_ptr<DecoderBinding> binding;
    BufferPtr device;
    CodecContextPtr decoder;
    FfmpegDecodeDevice decodeDevice = FfmpegDecodeDevice::Processor;
    DeliveryPlan plan;
    SequenceRate timeBase;
    std::optional<SequenceRate> nominalFrameRate;
    bool slices = false;
};

/// Sends a second reader's packets to a device's decoder until a frame comes out, and transfers it.
/// @return whether the device decoded a frame the stream's delivery can take; a failure only for the file's own
///         read failure or the execution's cancellation, which end the attempt rather than rule out the device.
Result<bool> deviceDecodesAFrame(const ExecutionContext& execution, OpenedContainer& probe, AVCodecContext& decoder,
                                 AVPixelFormat stored)
{
    const BoundOperation bound(*probe.input, execution);
    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    FramePtr transferred(av_frame_alloc());
    if (!packet || !frame || !transferred)
        return Status::failure(ErrorCode::AllocationFailed);
    bool draining = false;
    for (;;)
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const int received = avcodec_receive_frame(&decoder, frame.get());
        if (received == 0)
            return frame->hw_frames_ctx != nullptr &&
                   av_hwframe_transfer_data(transferred.get(), frame.get(), 0) >= 0 &&
                   deliverable(*transferred, stored);
        if (received != errorAgain() || draining)
            return false;
        for (;;)
        {
            const int read = av_read_frame(probe.format.get(), packet.get());
            if (read == errorEndOfFile())
            {
                draining = true;
                (void)avcodec_send_packet(&decoder, nullptr);
                break;
            }
            if (read < 0)
                return inputFailure(*probe.input, execution, read, "a packet could not be read");
            if (packet->stream_index != probe.stream)
            {
                av_packet_unref(packet.get());
                continue;
            }
            const int sent = avcodec_send_packet(&decoder, packet.get());
            av_packet_unref(packet.get());
            if (sent < 0 && sent != errorAgain())
                return false;
            break;
        }
    }
}

/// A decoder on the device a choice names, when the device opens, the coding has a decoder for it and it decodes a
/// frame of the stream through a second reader of the file; nothing otherwise.
Result<std::optional<std::pair<CodecContextPtr, BufferPtr>>>
openDeviceDecoder(const ExecutionContext& execution, const OpenedContainer& container, const AVCodec& codec,
                  DecoderBinding& binding, const DeviceChoice& choice, AVPixelFormat stored)
{
    using Opened = std::optional<std::pair<CodecContextPtr, BufferPtr>>;
    if (container.index.frameCount == 0u)
        return Opened{};
    const AVHWDeviceType type = av_hwdevice_find_type_by_name(choice.type);
    if (type == AV_HWDEVICE_TYPE_NONE)
        return Opened{};
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    for (int configuration = 0;; ++configuration)
    {
        const AVCodecHWConfig* offered = avcodec_get_hw_config(&codec, configuration);
        if (offered == nullptr)
            break;
        if ((offered->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 && offered->device_type == type)
        {
            pixelFormat = offered->pix_fmt;
            break;
        }
    }
    if (pixelFormat == AV_PIX_FMT_NONE)
        return Opened{};
    AVBufferRef* created = nullptr;
    if (av_hwdevice_ctx_create(&created, type, nullptr, nullptr, 0) < 0)
        return Opened{};
    BufferPtr device(created);
    const AVStream& stream = *container.format->streams[container.stream];
    CodecContextPtr decoder(avcodec_alloc_context3(&codec));
    if (!decoder)
        return Status::failure(ErrorCode::AllocationFailed);
    if (avcodec_parameters_to_context(decoder.get(), stream.codecpar) < 0)
        return Opened{};
    decoder->pkt_timebase = stream.time_base;
    decoder->thread_count = 1;
    decoder->thread_type = 0;
    decoder->opaque = &binding;
    decoder->get_format = &chooseDeviceFormat;
    decoder->hw_device_ctx = av_buffer_ref(device.get());
    if (decoder->hw_device_ctx == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    binding.devicePixelFormat = pixelFormat;
    if (avcodec_open2(decoder.get(), &codec, nullptr) < 0)
        return Opened{};
    auto probe = openAgain(container, execution);
    if (!probe.hasValue())
        return probe.status();
    auto proven = deviceDecodesAFrame(execution, *probe, *decoder, stored);
    if (!proven.hasValue())
        return proven.status();
    if (!*proven)
        return Opened{};
    avcodec_flush_buffers(decoder.get());
    return Opened{std::pair{std::move(decoder), std::move(device)}};
}

Result<PreparedVideo> prepare(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                              const FfmpegSourceOptions& options)
{
    if (!engine.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
        return checked;
    if (!isBoundaryPath(path))
        return argumentFailure("a video path is nonempty bounded UTF-8 without NUL", "path");
    if (const Status checked = checkOptions(options); checked.failed())
        return checked;
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    auto container = openContainer(engine, execution, path, options);
    if (!container.hasValue())
        return container.status();
    AVFormatContext& format = *container->format;
    AVStream& stream = *format.streams[container->stream];
    const AVCodecParameters& parameters = *stream.codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters.codec_id);
    if (codec == nullptr || av_match_name(codec->name, kVideoDecoders) == 0)
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            "the stream's codec is not an admitted video decoder", "codec", path);
    const AVRational aspect = av_guess_sample_aspect_ratio(&format, &stream, nullptr);
    const double pixelAspectRatio =
        aspect.num > 0 && aspect.den > 0 ? static_cast<double>(aspect.num) / static_cast<double>(aspect.den) : 1.0;
    auto plan = planDelivery(parameters, options, engine.limits(), pixelAspectRatio, path);
    if (!plan.hasValue())
        return plan.status();
    if (stream.time_base.num <= 0 || stream.time_base.den <= 0)
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "the stream states no time base", "timeBase", path);
    CodecContextPtr decoder(avcodec_alloc_context3(codec));
    if (!decoder)
        return Status::failure(ErrorCode::AllocationFailed);
    decoder->codec_whitelist = av_strdup(kVideoDecoders);
    if (decoder->codec_whitelist == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    if (const int copied = avcodec_parameters_to_context(decoder.get(), &parameters); copied < 0)
        return libraryFailure(copied, "the decoder could not take the stream's parameters", path);
    decoder->pkt_timebase = stream.time_base;
    decoder->thread_count = 1;
    decoder->thread_type = 0;
    auto binding = std::make_unique<DecoderBinding>();
    decoder->opaque = binding.get();
    // FFV1 slices are independent by construction - each carries its own coder state - so they may run
    // at once. Other decoders keep FFmpeg's serial default until the same has been established for them.
    const bool slices =
        parameters.codec_id == AV_CODEC_ID_FFV1 && (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) != 0;
    if (slices)
        decoder->execute = &runSlices;
    if (const int opened = avcodec_open2(decoder.get(), codec, nullptr); opened < 0)
        return libraryFailure(opened, "the decoder could not be opened", path);
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;

    // The processor's decoder is open; a device replaces it only once the device has decoded a frame.
    BufferPtr device;
    FfmpegDecodeDevice decodeDevice = FfmpegDecodeDevice::Processor;
    if (options.decodeDevice != FfmpegDecodeDevice::Processor)
    {
        for (const DeviceChoice& choice : kDeviceChoices)
        {
            const bool tried = options.decodeDevice == FfmpegDecodeDevice::Automatic
                                   ? choice.device != FfmpegDecodeDevice::Vulkan
                                   : choice.device == options.decodeDevice;
            if (!tried)
                continue;
            auto deviceBinding = std::make_unique<DecoderBinding>();
            auto opened = openDeviceDecoder(execution, *container, *codec, *deviceBinding, choice, plan->stored);
            if (!opened.hasValue())
                return opened.status();
            if (!*opened)
                continue;
            decoder = std::move((*opened)->first);
            device = std::move((*opened)->second);
            binding = std::move(deviceBinding);
            decodeDevice = choice.device;
            execution.reportTrace(TraceEventKind::Instant, "ffmpeg", "device-decoder-opened");
            break;
        }
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
    }

    PreparedVideo video;
    video.timeBase = SequenceRate{static_cast<std::uint32_t>(stream.time_base.num),
                                  static_cast<std::uint32_t>(stream.time_base.den)};
    if (stream.avg_frame_rate.num > 0 && stream.avg_frame_rate.den > 0)
        video.nominalFrameRate = SequenceRate{static_cast<std::uint32_t>(stream.avg_frame_rate.num),
                                              static_cast<std::uint32_t>(stream.avg_frame_rate.den)};
    video.container = std::move(*container);
    video.binding = std::move(binding);
    video.device = std::move(device);
    video.decoder = std::move(decoder);
    video.decodeDevice = decodeDevice;
    video.plan = std::move(*plan);
    video.slices = slices && decodeDevice == FfmpegDecodeDevice::Processor;
    return video;
}

SequenceFormat formatOf(const char* name) noexcept
{
    const std::string_view text(name != nullptr ? name : "");
    if (text.find("mp4") != std::string_view::npos)
        return SequenceFormat::Mp4;
    if (text == "avi")
        return SequenceFormat::Avi;
    return SequenceFormat::Video;
}

std::uint8_t codePoint(int value) noexcept
{
    return value >= 0 && value <= 255 ? static_cast<std::uint8_t>(value) : std::uint8_t{2u};
}

FfmpegStreamInfo infoOf(const PreparedVideo& video)
{
    const AVFormatContext& format = *video.container.format;
    const AVStream& stream = *format.streams[video.container.stream];
    const AVCodecParameters& parameters = *stream.codecpar;
    const StreamIndex& index = video.container.index;
    const char* pixelFormat = av_get_pix_fmt_name(video.plan.stored);
    return FfmpegStreamInfo{.format = formatOf(format.iformat->name),
                            .containerName = format.iformat->name,
                            .codecName = avcodec_get_name(parameters.codec_id),
                            .storedPixelFormat = pixelFormat != nullptr ? pixelFormat : "",
                            .streamIndex = static_cast<std::uint32_t>(video.container.stream),
                            .storedBitsPerComponent = video.plan.storedBits,
                            .matrixCoefficients = codePoint(parameters.color_space),
                            .colourPrimaries = codePoint(parameters.color_primaries),
                            .transferCharacteristics = codePoint(parameters.color_trc),
                            .colorRange = colorRangeOf(parameters.color_range),
                            .conversion = video.plan.conversion,
                            .conversionMatrix = video.plan.conversionMatrix,
                            .conversionRange = video.plan.conversionRange,
                            .frame = *video.plan.frame,
                            .frameCount = index.frameCount,
                            .keyframeCount = index.keyframeCount,
                            .hiddenPacketCount = index.hiddenCount,
                            .timeBase = video.timeBase,
                            .nominalFrameRate = video.nominalFrameRate,
                            .durationTicks = index.timelineEnd,
                            .reordered = index.reordered,
                            .slicesOnOperationThreads = video.slices,
                            .decodeDevice = video.decodeDevice};
}

Result<ImageMetadata> metadataOf(const PreparedVideo& video, const FfmpegStreamInfo& info)
{
    const AVCodecParameters& parameters = *video.container.format->streams[video.container.stream]->codecpar;
    const std::array<std::pair<const char*, std::string>, 15u> fields{
        {{"container", info.containerName},
         {"codec", info.codecName},
         {"pixelFormat", info.storedPixelFormat},
         {"streamIndex", std::to_string(info.streamIndex)},
         {"bitsPerComponent", std::to_string(info.storedBitsPerComponent)},
         {"colorRange", colorRangeName(info.colorRange)},
         {"matrixCoefficients", std::to_string(info.matrixCoefficients)},
         {"colourPrimaries", std::to_string(info.colourPrimaries)},
         {"transferCharacteristics", std::to_string(info.transferCharacteristics)},
         {"chromaLocation", std::to_string(static_cast<int>(parameters.chroma_location))},
         {"fieldOrder", std::to_string(static_cast<int>(parameters.field_order))},
         {"conversion", conversionName(info.conversion)},
         {"conversionMatrix", std::to_string(info.conversionMatrix)},
         {"conversionRange", colorRangeName(info.conversionRange)},
         {"decodeDevice", ffmpegDecodeDeviceName(info.decodeDevice)}}};
    std::vector<CodecCard> cards;
    cards.reserve(fields.size());
    for (const auto& [keyword, value] : fields)
    {
        auto card = CodecCard::create(CodecNamespace::Ffmpeg, keyword, value, {});
        if (!card.hasValue())
            return card.status();
        cards.push_back(std::move(*card));
    }
    auto codec = CodecMetadata::create(video.plan.source, std::move(cards));
    if (!codec.hasValue())
        return codec.status();
    return ImageMetadata::create(AcquisitionMetadata{}, {}, {}, {}, std::move(*codec), std::nullopt);
}

class FfmpegSourceBackend final : public FrameSourceBackend
{
public:
    FfmpegSourceBackend(PreparedVideo video, ImageMetadata metadata, const FfmpegSourceOptions& options,
                        std::string path, PacketPtr packet, FramePtr frame)
        : m_video(std::move(video)), m_delivery(m_video.plan), m_packet(std::move(packet)), m_frame(std::move(frame)),
          m_metadata(std::move(metadata)), m_options(options), m_path(std::move(path))
    {
    }

    Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext& execution,
                                    std::uint64_t index) override
    {
        const BoundOperation bound(*m_video.container.input, execution);
        const BoundDecoder slices(*m_video.binding, execution);
        auto decoded = decode(execution, index);
        if (!decoded.hasValue())
            return decoded.status();
        if ((*decoded)->hw_frames_ctx != nullptr)
        {
            auto transferred = fromDevice(execution, **decoded);
            if (!transferred.hasValue())
                return transferred.status();
            *decoded = std::move(*transferred);
        }
        auto raw = m_delivery.deliver(engine, execution, **decoded, m_options, m_path);
        if (!raw.hasValue())
            return raw.status();
        const StreamIndex& streamIndex = m_video.container.index;
        const FrameEntry& entry = streamIndex.frameEntries()[static_cast<std::size_t>(index)];
        const SequencePosition position{index, streamIndex.ticksOf(index),
                                        entry.duration > 0 ? std::optional<std::int64_t>(entry.duration)
                                                           : std::nullopt};
        return SequenceReadResult{SequenceFrame{std::move(*raw), m_metadata, position}};
    }

    Result<SequencePosition> seek(const ExecutionContext&, const SequenceSeekRequest& request) override
    {
        const StreamIndex& streamIndex = m_video.container.index;
        if (request.kind == SequenceSeekKind::Index)
            return SequencePosition{request.index,
                                    request.index < streamIndex.frameCount
                                        ? std::optional<std::int64_t>(streamIndex.ticksOf(request.index))
                                        : std::nullopt,
                                    std::nullopt};
        const std::span<FrameEntry> frames = streamIndex.frameEntries();
        if (frames.empty())
            return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
        const std::int64_t first = frames.front().pts;
        const std::int64_t target = first > 0 && request.ticks > (std::numeric_limits<std::int64_t>::max)() - first
                                        ? (std::numeric_limits<std::int64_t>::max)()
                                        : first + request.ticks;
        std::uint64_t index = 0u;
        if (request.kind == SequenceSeekKind::TimeAtOrAfter)
        {
            const auto found =
                std::lower_bound(frames.begin(), frames.end(), target,
                                 [](const FrameEntry& frame, std::int64_t value) { return frame.pts < value; });
            if (found == frames.end())
                return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
            index = static_cast<std::uint64_t>(found - frames.begin());
        }
        else
        {
            const auto found =
                std::upper_bound(frames.begin(), frames.end(), target,
                                 [](std::int64_t value, const FrameEntry& frame) { return value < frame.pts; });
            if (found == frames.begin())
                return sequenceFailure(ErrorCode::OutOfRange, SequenceFailure::InvalidPosition);
            index = static_cast<std::uint64_t>(found - frames.begin()) - 1u;
        }
        return SequencePosition{index, streamIndex.ticksOf(index), std::nullopt};
    }

    Status close() override
    {
        m_frame.reset();
        m_packet.reset();
        m_video.decoder.reset();
        m_video.container.format.reset();
        int error = 0;
        if (m_video.container.input)
        {
            error = m_video.container.input->close();
            m_video.container.input.reset();
        }
        m_video.container.index = StreamIndex{};
        if (error != 0)
            return ioFailure(error, "the video file could not be closed", m_path);
        return Status::success();
    }

private:
    /// Transfers a frame a device decoded to the processor and rearranges it into the stream's stored format.
    Result<FramePtr> fromDevice(const ExecutionContext& execution, const AVFrame& decoded)
    {
        FramePtr transferred(av_frame_alloc());
        if (!transferred)
            return Status::failure(ErrorCode::AllocationFailed);
        if (const int moved = av_hwframe_transfer_data(transferred.get(), &decoded, 0); moved < 0)
            return libraryFailure(moved, "the device could not transfer a decoded frame", m_path);
        if (const int copied = av_frame_copy_props(transferred.get(), &decoded); copied < 0)
            return libraryFailure(copied, "the device's frame properties could not be kept", m_path);
        execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "device-frames-transferred", 1.0, "frames");
        const AVPixelFormat stored = m_delivery.plan().stored;
        if (transferred->format == stored)
            return transferred;
        const Rearrangement* how = rearrangementOf(transferred->format, stored);
        if (how == nullptr)
            return videoFailure(ErrorCode::ExternalFailure, SequenceFailure::DecoderFailure,
                                "the device delivered a pixel format the stream's cannot be rearranged from",
                                "pixelFormat", m_path);
        FramePtr rearranged(av_frame_alloc());
        if (!rearranged)
            return Status::failure(ErrorCode::AllocationFailed);
        rearranged->format = stored;
        rearranged->width = transferred->width;
        rearranged->height = transferred->height;
        if (const int allocated = av_frame_get_buffer(rearranged.get(), 0); allocated < 0)
            return libraryFailure(allocated, "the rearranged frame could not be allocated", m_path);
        if (const int copied = av_frame_copy_props(rearranged.get(), transferred.get()); copied < 0)
            return libraryFailure(copied, "the device's frame properties could not be kept", m_path);
        rearrange(*transferred, *rearranged, *how);
        return rearranged;
    }

    [[nodiscard]] std::int64_t keyOf(const AVPacket& packet) const noexcept
    {
        return m_video.container.index.timestampsFromDecodeOrder ? packet.dts : packet.pts;
    }

    Result<FramePtr> decode(const ExecutionContext& execution, std::uint64_t index)
    {
        const StreamIndex& streamIndex = m_video.container.index;
        const FrameEntry target = streamIndex.frameEntries()[static_cast<std::size_t>(index)];
        // A frame is reachable by decoding forward when the run in progress began at or before its
        // keyframe, has already sent that keyframe, and has not yet output anything shown after it.
        const bool reachable = m_positioned && m_runStart <= target.start && target.start < m_sent &&
                               (m_lastPts == kNoTimestamp || target.pts > m_lastPts);
        if (!reachable)
            if (const Status moved = reposition(execution, target.start); moved.failed())
                return moved;
        for (;;)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const int received = avcodec_receive_frame(m_video.decoder.get(), m_frame.get());
            if (m_video.binding->sliceFailure.failed())
                return m_video.binding->sliceFailure;
            if (received == 0)
            {
                execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "frames-decoded", 1.0, "frames");
                const std::int64_t pts = m_frame->pts != kNoTimestamp ? m_frame->pts : m_frame->best_effort_timestamp;
                m_lastPts = pts;
                const auto position = streamIndex.frameAt(pts);
                if (position && *position == index)
                {
                    if ((m_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0)
                    {
                        av_frame_unref(m_frame.get());
                        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                            "the decoder reported the frame as corrupt", "frameIndex", m_path);
                    }
                    FramePtr delivered(av_frame_alloc());
                    if (!delivered)
                        return Status::failure(ErrorCode::AllocationFailed);
                    av_frame_move_ref(delivered.get(), m_frame.get());
                    return delivered;
                }
                av_frame_unref(m_frame.get());
                if ((position && *position > index) || (!position && pts > target.pts))
                    return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                        "the decoder did not produce the frame the index places here", "frameIndex",
                                        m_path, {StatusContextField::ofCount(ContextKey::Requested, index)});
                execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "frames-discarded", 1.0, "frames");
                continue;
            }
            if (received == errorEndOfFile())
                return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                    "the decoder ended before the frame the index places here", "frameIndex", m_path,
                                    {StatusContextField::ofCount(ContextKey::Requested, index)});
            if (received != errorAgain())
                return libraryFailure(received, "the decoder failed", m_path);
            if (const Status sent = sendNextPacket(execution); sent.failed())
                return sent;
        }
    }

    Status sendNextPacket(const ExecutionContext& execution)
    {
        const StreamIndex& streamIndex = m_video.container.index;
        for (;;)
        {
            if (m_demuxEnded)
            {
                if (m_draining)
                    return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                        "the decoder ended before the frame the index places here", "frameIndex",
                                        m_path);
                m_draining = true;
                const int flushed = avcodec_send_packet(m_video.decoder.get(), nullptr);
                if (flushed < 0 && flushed != errorEndOfFile())
                    return libraryFailure(flushed, "the decoder could not be drained", m_path);
                return Status::success();
            }
            if (!m_pending)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                    return stopped;
                const int read = av_read_frame(m_video.container.format.get(), m_packet.get());
                if (read == errorEndOfFile())
                {
                    m_demuxEnded = true;
                    continue;
                }
                if (read < 0)
                    return inputFailure(*m_video.container.input, execution, read, "a packet could not be read");
                if (m_packet->stream_index != m_video.container.stream)
                {
                    av_packet_unref(m_packet.get());
                    continue;
                }
            }
            m_pending = false;
            const auto ordinal = streamIndex.packetAt(keyOf(*m_packet));
            if (!ordinal || *ordinal != m_expected)
            {
                av_packet_unref(m_packet.get());
                return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                    "the container's packets differ from the ones it held when it was indexed",
                                    "packet", m_path);
            }
            ++m_expected;
            if (*ordinal < m_skipUntil)
            {
                av_packet_unref(m_packet.get());
                continue;
            }
            // A palette handed over beside a packet reached the index scan and no later reader, so a run gives
            // the decoder the palette in effect with its first packet, and each change with the packet it
            // arrived with.
            if (const PaletteRecord* palette = streamIndex.paletteAt(*ordinal);
                palette != nullptr && palette != m_palette)
            {
                std::size_t attached = 0u;
                if (av_packet_get_side_data(m_packet.get(), AV_PKT_DATA_PALETTE, &attached) == nullptr)
                {
                    std::uint8_t* side = av_packet_new_side_data(m_packet.get(), AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
                    if (side == nullptr)
                    {
                        av_packet_unref(m_packet.get());
                        return Status::failure(ErrorCode::AllocationFailed);
                    }
                    std::memcpy(side, palette->entries.data(), palette->entries.size());
                }
                m_palette = palette;
            }
            const int sent = avcodec_send_packet(m_video.decoder.get(), m_packet.get());
            av_packet_unref(m_packet.get());
            if (sent < 0)
                return libraryFailure(sent, "the decoder refused a packet", m_path);
            m_sent = *ordinal + 1u;
            execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "packets-sent", 1.0, "packets");
            return Status::success();
        }
    }

    Status reposition(const ExecutionContext& execution, std::uint32_t start)
    {
        execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "repositions", 1.0, "repositions");
        const StreamIndex& streamIndex = m_video.container.index;
        avcodec_flush_buffers(m_video.decoder.get());
        av_packet_unref(m_packet.get());
        av_frame_unref(m_frame.get());
        m_positioned = false;
        m_pending = false;
        m_draining = false;
        m_demuxEnded = false;
        m_lastPts = kNoTimestamp;
        m_palette = nullptr;
        const PacketEntry& key = streamIndex.packetEntries()[start];
        const std::int64_t target = key.dts != kNoTimestamp ? key.dts : key.pts;
        bool landed = false;
        // Ask the demuxer for the last position at or before the keyframe, then recognise where it
        // actually put the reader: demuxers disagree about what their seek timestamps mean.
        const int sought = avformat_seek_file(m_video.container.format.get(), m_video.container.stream,
                                              (std::numeric_limits<std::int64_t>::min)(), target, target, 0);
        if (sought >= 0)
        {
            for (;;)
            {
                const int read = av_read_frame(m_video.container.format.get(), m_packet.get());
                if (read == errorEndOfFile())
                    break;
                if (read < 0)
                    return inputFailure(*m_video.container.input, execution, read,
                                        "a packet could not be read after a seek");
                if (m_packet->stream_index != m_video.container.stream)
                {
                    av_packet_unref(m_packet.get());
                    continue;
                }
                const auto ordinal = streamIndex.packetAt(keyOf(*m_packet));
                if (ordinal && *ordinal <= start)
                {
                    m_pending = true;
                    m_expected = *ordinal;
                    landed = true;
                }
                else
                {
                    av_packet_unref(m_packet.get());
                }
                break;
            }
        }
        else if (m_video.container.input->interrupted())
        {
            return inputFailure(*m_video.container.input, execution, sought, "the container could not be positioned");
        }
        if (!landed)
        {
            execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "restarts", 1.0, "restarts");
            if (const Status reopened = reopenContainer(m_video.container, execution); reopened.failed())
                return reopened;
            m_expected = 0u;
        }
        m_skipUntil = start;
        m_runStart = start;
        m_sent = start;
        m_positioned = true;
        return Status::success();
    }

    PreparedVideo m_video;
    FrameDelivery m_delivery;
    PacketPtr m_packet;
    FramePtr m_frame;
    ImageMetadata m_metadata;
    FfmpegSourceOptions m_options;
    std::string m_path;
    bool m_positioned = false;
    bool m_pending = false;
    bool m_draining = false;
    bool m_demuxEnded = false;
    std::uint32_t m_runStart = 0u;
    std::uint32_t m_sent = 0u;
    std::uint32_t m_skipUntil = 0u;
    std::uint32_t m_expected = 0u;
    std::int64_t m_lastPts = kNoTimestamp;
    const PaletteRecord* m_palette = nullptr;
};

} // namespace
} // namespace an::ffmpeg_detail

namespace an
{

const char* ffmpegDecodeDeviceName(FfmpegDecodeDevice device) noexcept
{
    switch (device)
    {
    case FfmpegDecodeDevice::Processor:
        return "processor";
    case FfmpegDecodeDevice::Automatic:
        return "auto";
    case FfmpegDecodeDevice::Cuda:
        return "cuda";
    case FfmpegDecodeDevice::D3d11va:
        return "d3d11va";
    case FfmpegDecodeDevice::Vaapi:
        return "vaapi";
    case FfmpegDecodeDevice::VideoToolbox:
        return "videotoolbox";
    case FfmpegDecodeDevice::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

Result<FfmpegStreamInfo> inspectFfmpegFile(const EngineContext& engine, const ExecutionContext& execution,
                                           std::string_view path, const FfmpegSourceOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<FfmpegStreamInfo> {
        auto video = ffmpeg_detail::prepare(engine, execution, path, options);
        if (!video.hasValue())
            return video.status();
        return ffmpeg_detail::infoOf(*video);
    });
}

Result<FrameSource> createFfmpegSource(const EngineContext& engine, const ExecutionContext& execution,
                                       std::string_view path, const FfmpegSourceOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<FrameSource> {
        auto video = ffmpeg_detail::prepare(engine, execution, path, options);
        if (!video.hasValue())
            return video.status();
        FfmpegStreamInfo info = ffmpeg_detail::infoOf(*video);
        auto metadata = ffmpeg_detail::metadataOf(*video, info);
        if (!metadata.hasValue())
            return metadata.status();
        ffmpeg_detail::PacketPtr packet(av_packet_alloc());
        ffmpeg_detail::FramePtr frame(av_frame_alloc());
        if (!packet || !frame)
            return Status::failure(ErrorCode::AllocationFailed);
        const SequenceDescriptor descriptor{.format = info.format,
                                            .fixedFrame = info.frame,
                                            .frameCount = info.frameCount,
                                            .timeBase = info.timeBase,
                                            .nominalFrameRate = info.nominalFrameRate,
                                            .durationTicks = info.durationTicks,
                                            .timestampsRequired = true,
                                            .seekByIndex = true,
                                            .seekByTime = true};
        auto backend = std::make_unique<ffmpeg_detail::FfmpegSourceBackend>(
            std::move(*video), std::move(*metadata), options, std::string(path), std::move(packet), std::move(frame));
        return FrameSource::create(engine, descriptor, std::move(backend));
    });
}

} // namespace an
