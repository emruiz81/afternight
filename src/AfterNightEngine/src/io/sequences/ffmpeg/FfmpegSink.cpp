// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg video sink: frames encoded at a constant rate into an MP4, Matroska or WebM output.
//
// Creation settles everything that can refuse before a frame arrives: the encode plan, the encoder's
// threads, the muxer, the first candidate encoder that opens, and the container header. The threads are
// leased from the engine context all together or not at all, because some encoders write a different
// stream at a different thread count. Each frame is converted into the encoder's 4:2:0 picture -
// rearranged into NV12 for a device encoder, and uploaded to a surface for VA-API - stamped with its
// ordinal as its timestamp, and its packets are muxed as they come back. `finish` drains the encoder,
// writes the trailer, releases every library object and the thread lease, closes the file, and only
// then completes the output's claim; an unfinished container is truncated to nothing.

#include <AfterNightEngine/IO/FfmpegSink.h>

#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include "io/sequences/SequenceInternal.h"
#include "io/sequences/ffmpeg/FfmpegEncoding.h"
#include "io/sequences/ffmpeg/FfmpegLibrary.h"
#include "io/sequences/ffmpeg/FfmpegOutput.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace an
{
namespace
{
using ffmpeg_detail::EncodePlan;
using ffmpeg_detail::OutputFile;

class FfmpegSinkBackend final : public FrameSinkBackend
{
public:
    FfmpegSinkBackend(std::unique_ptr<OutputFile> output, SequenceOutputClaim claim, const EncodePlan& plan,
                      EngineContextLease threads)
        : m_output(std::move(output)), m_claim(std::move(claim)), m_pendingPath(m_claim.pendingPath()), m_plan(plan),
          m_threads(std::move(threads))
    {
    }

    ~FfmpegSinkBackend() override
    {
        (void)discard();
        m_claim.release();
    }

    FfmpegSinkBackend(const FfmpegSinkBackend&) = delete;
    FfmpegSinkBackend& operator=(const FfmpegSinkBackend&) = delete;

    /// Which encoder opened and which candidates refused before it.
    [[nodiscard]] const FfmpegEncoderReport& report() const noexcept
    {
        return m_report;
    }

    /// Opens the muxer and the first candidate encoder that opens, and writes the container's header. A failure
    /// leaves the payload for the destructor to discard.
    Status open(const ExecutionContext& execution)
    {
        const ffmpeg_detail::BoundOutput bound(*m_output, execution);
        if (av_guess_format(m_plan.muxerName, nullptr, nullptr) == nullptr)
            return ffmpeg_detail::videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                               "this build's FFmpeg has no requested muxer", "container",
                                               m_pendingPath);
        AVFormatContext* raw = nullptr;
        if (const int allocated = avformat_alloc_output_context2(&raw, nullptr, m_plan.muxerName, nullptr);
            allocated < 0 || raw == nullptr)
            return ffmpeg_detail::encoderFailure(allocated < 0 ? allocated : ffmpeg_detail::errorInvalidArgument(),
                                                 "the muxer could not be created", "container", m_pendingPath);
        m_muxer.reset(raw);
        if (const Status configured = ffmpeg_detail::configureMuxer(*m_muxer, *m_output); configured.failed())
            return configured;
        const bool globalHeader = (m_muxer->oformat->flags & AVFMT_GLOBALHEADER) != 0;
        Status refusal = ffmpeg_detail::videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                                     "this build's FFmpeg has none of the coding's encoders", "codec",
                                                     m_pendingPath);
        for (const ffmpeg_detail::EncoderCandidate& candidate :
             ffmpeg_detail::encoderCandidatesOf(m_plan.codec, m_plan.preference))
        {
            if (avcodec_find_encoder_by_name(candidate.name) == nullptr)
                continue;
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            auto encoder = ffmpeg_detail::openEncoder(m_plan, candidate, globalHeader, m_pendingPath);
            if (!encoder.hasValue())
            {
                refusal = encoder.status();
                if (candidate.family != ffmpeg_detail::EncoderFamily::Software)
                    m_report.refused.emplace_back(candidate.name);
                execution.reportTrace(TraceEventKind::Instant, "ffmpeg", "encoder-refused");
                continue;
            }
            m_encoder = std::move(encoder->context);
            m_device = std::move(encoder->device);
            m_surfaces = std::move(encoder->frames);
            m_family = candidate.family;
            m_report.encoderName = candidate.name;
            m_report.device = candidate.family != ffmpeg_detail::EncoderFamily::Software;
            break;
        }
        if (!m_encoder)
            return refusal;
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        m_stream = avformat_new_stream(m_muxer.get(), nullptr);
        if (m_stream == nullptr)
            return Status::failure(ErrorCode::AllocationFailed);
        if (const int copied = avcodec_parameters_from_context(m_stream->codecpar, m_encoder.get()); copied < 0)
            return ffmpeg_detail::encoderFailure(copied, "the stream could not take the encoder's parameters",
                                                 "container", m_pendingPath);
        m_stream->time_base = m_plan.timeBase;
        m_stream->avg_frame_rate = m_plan.frameRate;
        // An MP4 writes its colour box only when every colour field is stated unless it is asked to, and a grey
        // stream states no matrix; a limited-range grey H.264 stream carries no signal type of its own, so without
        // the box nothing in the file would state its code range.
        AVDictionary* header = nullptr;
        if (m_plan.container == FfmpegVideoContainer::Mp4 && av_dict_set(&header, "movflags", "+write_colr", 0) < 0)
            return Status::failure(ErrorCode::AllocationFailed);
        const int written = avformat_write_header(m_muxer.get(), &header);
        const bool unconsumed = av_dict_count(header) != 0;
        av_dict_free(&header);
        if (written < 0)
            return ffmpeg_detail::outputFailure(*m_output, execution, written,
                                                "the container header could not be written", "container");
        if (unconsumed)
            return Status::failure(ErrorCode::Internal,
                                   {.message = "the muxer did not take every setting the adapter gave it",
                                    .operation = "ffmpeg",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "container")}});
        // Header writing can replace the requested time base. Guard the actual library result too:
        // every input frame period must span at least one stored timestamp tick.
        if (m_stream->time_base.num <= 0 || m_stream->time_base.den <= 0 ||
            av_cmp_q(m_stream->time_base, m_plan.timeBase) > 0)
            return ffmpeg_detail::videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                               "the muxer cannot distinguish frames at the requested rate",
                                               "nominalFrameRate", m_pendingPath);
        m_picture.reset(av_frame_alloc());
        m_packet.reset(av_packet_alloc());
        if (!m_picture || !m_packet)
            return Status::failure(ErrorCode::AllocationFailed);
        m_picture->format = AV_PIX_FMT_YUV420P;
        m_picture->width = m_plan.width;
        m_picture->height = m_plan.height;
        if (const int allocated = av_frame_get_buffer(m_picture.get(), 0); allocated < 0)
            return ffmpeg_detail::encoderFailure(allocated, "the encoder's picture could not be allocated", "extent",
                                                 m_pendingPath);
        if (m_family != ffmpeg_detail::EncoderFamily::Software)
        {
            m_semiPlanar.reset(av_frame_alloc());
            if (!m_semiPlanar)
                return Status::failure(ErrorCode::AllocationFailed);
            m_semiPlanar->format = AV_PIX_FMT_NV12;
            m_semiPlanar->width = m_plan.width;
            m_semiPlanar->height = m_plan.height;
            if (const int allocated = av_frame_get_buffer(m_semiPlanar.get(), 0); allocated < 0)
                return ffmpeg_detail::encoderFailure(allocated, "the device encoder's picture could not be allocated",
                                                     "extent", m_pendingPath);
        }
        return Status::success();
    }

    Result<SequenceWriteResult> append(const EngineContext&, const ExecutionContext& execution,
                                       const SequenceFrame& frame) override
    {
        const ImageLayout layout = *frame.frame.layout();
        const bool storable = m_plan.grey ? layout.planeCount() == 1u && layout.samplesPerPixel() == 1u
                                          : (layout.planeCount() == 1u && layout.samplesPerPixel() == 3u) ||
                                                (layout.planeCount() == 3u && layout.samplesPerPixel() == 1u);
        if (!storable)
            return ffmpeg_detail::videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                               "a video frame is one plane of samples or three planes of RGB", "layout",
                                               m_pendingPath);
        if (!m_plan.grey && layout.rowStrideBytes() > static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
            return ffmpeg_detail::videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                                               "the colour converter requires a row stride representable as int",
                                               "rowStrideBytes", m_pendingPath);
        const ffmpeg_detail::BoundOutput bound(*m_output, execution);
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        // The encoder may still hold the previous picture; writing a new one into shared buffers would change it.
        if (const int writable = av_frame_make_writable(m_picture.get()); writable < 0)
            return ffmpeg_detail::encoderFailure(writable, "the encoder's picture could not be made writable", "extent",
                                                 m_pendingPath);
        if (m_plan.grey)
        {
            ffmpeg_detail::convertGreyFrame(m_plan, frame.frame, *m_picture);
        }
        else
        {
            const AVPixelFormat source = ffmpeg_detail::sourceFormatOf(layout);
            ffmpeg_detail::ScaleContextPtr& converter = source == AV_PIX_FMT_GBRP ? m_planarConverter : m_converter;
            if (!converter)
            {
                auto opened = ffmpeg_detail::openConverter(m_plan, source);
                if (!opened.hasValue())
                    return opened.status();
                converter = std::move(*opened);
            }
            if (const Status converted =
                    ffmpeg_detail::convertFrame(*converter, m_plan, frame.frame, layout, *m_picture, m_pendingPath);
                converted.failed())
                return converted;
        }
        AVFrame* picture = m_picture.get();
        if (m_family != ffmpeg_detail::EncoderFamily::Software)
        {
            if (const int writable = av_frame_make_writable(m_semiPlanar.get()); writable < 0)
                return ffmpeg_detail::encoderFailure(
                    writable, "the device encoder's picture could not be made writable", "extent", m_pendingPath);
            ffmpeg_detail::rearrangeToNv12(*m_picture, *m_semiPlanar);
            picture = m_semiPlanar.get();
            if (m_family == ffmpeg_detail::EncoderFamily::Vaapi)
            {
                ffmpeg_detail::FramePtr surface(av_frame_alloc());
                if (!surface)
                    return Status::failure(ErrorCode::AllocationFailed);
                if (const int reserved = av_hwframe_get_buffer(m_surfaces.get(), surface.get(), 0); reserved < 0)
                    return ffmpeg_detail::encoderFailure(reserved, "a VA-API surface could not be allocated", "codec",
                                                         m_pendingPath);
                if (const int moved = av_hwframe_transfer_data(surface.get(), m_semiPlanar.get(), 0); moved < 0)
                    return ffmpeg_detail::encoderFailure(moved, "a picture could not be uploaded to its VA-API surface",
                                                         "codec", m_pendingPath);
                m_surface = std::move(surface);
                picture = m_surface.get();
            }
        }
        picture->pts = static_cast<std::int64_t>(m_frames);
        if (const Status encoded = encode(execution, picture); encoded.failed())
            return encoded;
        ++m_frames;
        execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "frames-encoded", 1.0, "frames");
        return SequenceWriteResult{m_frames, m_output->extent()};
    }

    Result<SequenceWriteResult> finish(const ExecutionContext& execution) override
    {
        const ffmpeg_detail::BoundOutput bound(*m_output, execution);
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (const Status drained = encode(execution, nullptr); drained.failed())
            return drained;
        if (const int written = av_write_trailer(m_muxer.get()); written < 0)
            return ffmpeg_detail::outputFailure(*m_output, execution, written,
                                                "the container trailer could not be written", "container");
        if (const int flushed = m_output->flush(); flushed < 0)
            return ffmpeg_detail::outputFailure(*m_output, execution, flushed, "the container could not be written",
                                                "container");
        // The library's threads end with the encoder, and then the lease on them can go.
        release();
        const std::uint64_t bytes = m_output->extent();
        if (const int error = m_output->close(); error != 0)
            return ffmpeg_detail::ioFailure(error, "the video container could not be closed", m_pendingPath);
        m_finished = true;
        // The payload is complete and closed, so its output may commit it.
        m_claim.complete();
        m_claim.release();
        return SequenceWriteResult{m_frames, bytes};
    }

    Status close() override
    {
        const int error = discard();
        m_claim.release();
        if (error != 0)
            return ffmpeg_detail::ioFailure(error, "the unfinished video container could not be discarded",
                                            m_pendingPath);
        return Status::success();
    }

private:
    /// Sends a picture, or the end of the stream, and muxes every packet the encoder returns, each lasting one
    /// frame period so an MP4 edit list covers the last frame.
    Status encode(const ExecutionContext& execution, AVFrame* picture)
    {
        if (const int sent = avcodec_send_frame(m_encoder.get(), picture); sent < 0)
            return ffmpeg_detail::outputFailure(*m_output, execution, sent, "the encoder refused a frame", "codec");
        for (;;)
        {
            const int received = avcodec_receive_packet(m_encoder.get(), m_packet.get());
            if (received == ffmpeg_detail::errorAgain() || received == ffmpeg_detail::errorEndOfFile())
                return Status::success();
            if (received < 0)
                return ffmpeg_detail::outputFailure(*m_output, execution, received, "the encoder failed", "codec");
            if (m_packet->duration == 0)
                m_packet->duration = 1;
            av_packet_rescale_ts(m_packet.get(), m_encoder->time_base, m_stream->time_base);
            m_packet->stream_index = m_stream->index;
            if (const int muxed = av_interleaved_write_frame(m_muxer.get(), m_packet.get()); muxed < 0)
                return ffmpeg_detail::outputFailure(*m_output, execution, muxed, "a packet could not be written",
                                                    "container");
            execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "packets-written", 1.0, "packets");
        }
    }

    /// Releases every FFmpeg object and the lease on the encoder's threads, in that order.
    void release() noexcept
    {
        m_packet.reset();
        m_surface.reset();
        m_semiPlanar.reset();
        m_picture.reset();
        m_planarConverter.reset();
        m_converter.reset();
        m_encoder.reset();
        m_surfaces.reset();
        m_device.reset();
        m_muxer.reset();
        m_threads.release();
    }

    /// Discards an unfinished container down to nothing and closes it, reporting only a platform code, so the
    /// destructor can call it without building a failure. The output's reservation stands.
    int discard() noexcept
    {
        release();
        if (m_finished || !m_output)
            return 0;
        return m_output->discard();
    }

    std::unique_ptr<OutputFile> m_output;
    SequenceOutputClaim m_claim;
    std::string m_pendingPath;
    EncodePlan m_plan;
    EngineContextLease m_threads;
    ffmpeg_detail::OutputContextPtr m_muxer;
    ffmpeg_detail::CodecContextPtr m_encoder;
    ffmpeg_detail::BufferPtr m_device;
    ffmpeg_detail::BufferPtr m_surfaces;
    ffmpeg_detail::EncoderFamily m_family = ffmpeg_detail::EncoderFamily::Software;
    ffmpeg_detail::FramePtr m_semiPlanar;
    ffmpeg_detail::FramePtr m_surface;
    FfmpegEncoderReport m_report;
    AVStream* m_stream = nullptr;
    ffmpeg_detail::ScaleContextPtr m_converter;
    ffmpeg_detail::ScaleContextPtr m_planarConverter;
    ffmpeg_detail::FramePtr m_picture;
    ffmpeg_detail::PacketPtr m_packet;
    std::uint64_t m_frames = 0u;
    bool m_finished = false;
};

Status threadRefusal(ErrorCode code, const char* message, std::uint64_t requested, std::uint64_t available)
{
    return Status::failure(code, {.message = message,
                                  .operation = "ffmpeg",
                                  .domain = ErrorDomain::Io,
                                  .context = {StatusContextField::ofText(ContextKey::Parameter, "encoderThreads"),
                                              StatusContextField::ofCount(ContextKey::Requested, requested),
                                              StatusContextField::ofCount(ContextKey::Available, available)}});
}
} // namespace

bool isFfmpegEncoderAvailable(FfmpegVideoCodec codec) noexcept
{
    const char* name = ffmpeg_detail::encoderNameOf(codec);
    return name != nullptr && avcodec_find_encoder_by_name(name) != nullptr;
}

Result<FrameSink> createFfmpegSink(const EngineContext& engine, const ExecutionContext& execution,
                                   SequenceOutput& output, const SequenceDescriptor& descriptor,
                                   const FfmpegSinkRequest& request)
{
    FfmpegEncoderReport ignored;
    return createFfmpegSink(engine, execution, output, descriptor, request, ignored);
}

Result<FrameSink> createFfmpegSink(const EngineContext& engine, const ExecutionContext& execution,
                                   SequenceOutput& output, const SequenceDescriptor& descriptor,
                                   const FfmpegSinkRequest& request, FfmpegEncoderReport& report)
{
    return sequence_detail::guarded([&]() -> Result<FrameSink> {
        if (!engine.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
            return checked;
        if (!output.valid())
            return Status::failure(ErrorCode::InvalidState,
                                   {.message = "a video sink writes the pending payload of an uncommitted output",
                                    .operation = "ffmpeg",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        if (output.engineContextId() != engine.id())
            return Status::failure(ErrorCode::IncompatibleArguments,
                                   {.message = "the output was reserved for another engine context",
                                    .operation = "ffmpeg",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, "output")}});
        auto plan = ffmpeg_detail::planEncode(descriptor, request);
        if (!plan.hasValue())
            return plan.status();
        if (plan->encoderThreads > engine.maxConcurrency())
            return threadRefusal(ErrorCode::LimitExceeded, "an encoder cannot have more threads than its context",
                                 plan->encoderThreads, engine.maxConcurrency());
        // The sequence handle checks these again when it adopts the backend; checking them first keeps every
        // refusal ahead of the first byte written.
        if (const Status checked = sequence_detail::checkDescription(engine, descriptor); checked.failed())
            return checked;
        EngineContextLease threads;
        if (plan->encoderThreads > 1u)
        {
            auto lease = engine.acquireLease(plan->encoderThreads);
            if (!lease.hasValue())
                return threadRefusal(ErrorCode::Busy, "the context has no thread to lend the encoder",
                                     plan->encoderThreads, 0u);
            // The coded stream depends on the thread count for some encoders, so fewer threads than asked
            // for would write a different container for the same request.
            if (lease->admittedConcurrency() != plan->encoderThreads)
                return threadRefusal(ErrorCode::Busy,
                                     "the context cannot lend the encoder every thread it was asked for",
                                     plan->encoderThreads, lease->admittedConcurrency());
            threads = std::move(*lease);
        }
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        auto claim = output.claimWriter();
        if (!claim.hasValue())
            return claim.status();
        auto file = OutputFile::open(claim->pendingPath());
        if (!file.hasValue())
            return file.status();
        auto backend =
            std::make_unique<FfmpegSinkBackend>(std::move(*file), std::move(*claim), *plan, std::move(threads));
        if (const Status opened = backend->open(execution); opened.failed())
            return opened;
        FfmpegEncoderReport opened = backend->report();
        auto sink = FrameSink::create(engine, descriptor, std::move(backend));
        if (sink.hasValue())
            report = std::move(opened);
        return sink;
    });
}
} // namespace an
