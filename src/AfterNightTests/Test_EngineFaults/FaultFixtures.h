// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The subjects the fault campaign injects into: a small frame, a codec that can fail at every step
// it has, a source and a sink that can fail at any frame, and the throwing variants of all three.
//
// They are deliberately small and deliberately literal. A sweep runs its subject once per injection
// point, so a subject that takes a second to run is a scenario that takes minutes; and what is being
// tested is the engine's behaviour on a failure path, not a codec's arithmetic, so a frame that is
// eight by four and a source of twelve frames prove exactly as much as a real image would and cost
// nothing.
//
// The failure switches are ordinals rather than booleans, because the campaign's whole shape is a
// sweep: "fail at step k" and "throw at frame k" are what a scenario enumerates over, and a boolean
// would make each of them a hand-written case again.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/FrameSink.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace an::faults
{

/// The width of every frame this campaign writes, reads and streams.
inline constexpr std::uint32_t kFrameWidth = 8u;

/// The height of every frame this campaign writes, reads and streams.
inline constexpr std::uint32_t kFrameHeight = 4u;

/// How many bytes one of those frames holds.
inline constexpr std::uint64_t kFrameBytes = std::uint64_t{kFrameWidth} * kFrameHeight * 2u;

/// The description every frame in this campaign carries.
///
/// @return a sixteen-bit unsigned monochrome sensor frame.
[[nodiscard]] inline RawFrameDescriptor frameDescription()
{
    return *RawFrameDescriptor::create(
        {.extent = *ImageExtent::create(kFrameWidth, kFrameHeight, 1u, defaultEngineLimits()),
         .sampleFormat = {2u, RawSampleClass::UnsignedInteger},
         .byteOrder = ByteOrder::Native,
         .channelSchema = ChannelSchema::Mono,
         .colorSpace = ColorSpace::Unspecified,
         .numericDomain = NumericDomain::RawSensorCode,
         .coordinateOrigin = CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
}

/// The layout those samples are arranged in.
///
/// @param limits the engine context's limits, which the layout is checked against.
/// @return a tightly packed single-plane layout.
[[nodiscard]] inline ImageLayout frameLayout(const EngineLimits& limits)
{
    return *ImageLayout::create({frameDescription().extent(), 2u, 1u, 1u, 1u}, limits);
}

/// One frame of context-owned samples, filled from an index so two frames differ.
///
/// @param engine the context the samples are taken from.
/// @param seed what to fill the samples from.
/// @return the frame, or the allocation refusal that stopped it.
[[nodiscard]] inline Result<RawFrame> makeFrame(const EngineContext& engine, std::uint64_t seed = 0u)
{
    const ImageLayout layout = frameLayout(engine.limits());
    Result<SampleBuffer> buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Zeroed);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    Result<RawFrame> frame = RawFrame::create(frameDescription(), layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const auto bytes = frame->mutableBytes();
    if (!bytes.has_value())
    {
        return Status::failure(ErrorCode::Internal, "an owned frame refused to be written");
    }
    for (std::size_t i = 0u; i < bytes->size(); ++i)
    {
        (*bytes)[i] = static_cast<std::byte>((i * 7u + seed) & 0xffu);
    }
    return frame;
}

/// The sequence every source and sink in this campaign describes.
///
/// @param count how many frames it holds.
/// @return the descriptor.
[[nodiscard]] inline SequenceDescriptor frameSequence(std::uint64_t count)
{
    return {.format = SequenceFormat::ImageList, .fixedFrame = frameDescription(), .frameCount = count};
}

/// How a fixture is made to fail.
enum class FailureMode : std::uint8_t
{
    /// Returns a typed failure, which is what an external library's refusal becomes.
    Refuse,

    /// Throws, which is what an external library does and what the adapter boundary must translate.
    Throw
};

/// What the writing codec was told to do, shared by the codec and the scenario that armed it.
///
/// The steps are ordinals so a sweep can enumerate them: one is the encoder refusing before it
/// opens anything, two is a short payload, three is a payload larger than the encoder reports, four
/// is a failure after the bytes are on disk, and five is the encoder failing having written nothing.
struct WritingCodecPlan
{
    /// Which step to fail at, counting from one, or zero for a clean encode.
    std::uint64_t step = 0u;

    /// Whether that step refuses or throws.
    FailureMode mode = FailureMode::Refuse;

    /// How many encodes this codec has been asked for.
    std::atomic<std::uint64_t> encodes{0u};

    /// Whether the armed step was reached.
    std::atomic<bool> fired{false};
};

/// How many failure steps `WritingCodecPlan` enumerates.
inline constexpr std::uint64_t kWritingCodecSteps = 5u;

/// An encoder that writes the frame's bytes to the pending path it is given, and can fail at each
/// step of doing so.
///
/// It is a codec rather than a mock of one because the transaction's contract is stated in terms of
/// what the encoder did to the pending file - a payload shorter than the encoder reported, a payload
/// longer than it reported, bytes on disk under a failure - and none of those can be expressed by an
/// encoder that never writes.
class WritingCodec final : public ImageCodec
{
public:
    /// A codec that writes what it is given, under the plan it is given.
    ///
    /// @param plan what to do. Borrowed; must outlive this codec.
    explicit WritingCodec(WritingCodecPlan& plan) : m_plan(plan) {}

    /// What this codec can do.
    ///
    /// @return the FITS format with a sixteen-bit unsigned encoder and no decoder.
    [[nodiscard]] CodecCapabilities capabilities() const override
    {
        return {.format = CodecFormat::Fits,
                .decodeSamples = 0u,
                .encodeSamples = codecSampleBit({2u, RawSampleClass::UnsignedInteger}),
                .regionDecode = false};
    }

    /// Reads nothing: this codec exists to write.
    ///
    /// @return `ErrorCode::Unsupported`.
    [[nodiscard]] Result<CodecImageInfo> probe(const EngineContext&, const ExecutionContext&,
                                               const CodecReadRequest&) const override
    {
        return Status::failure(ErrorCode::Unsupported);
    }

    /// Writes the frame's bytes to the pending path, failing at the armed step.
    ///
    /// @param request where to write and in what format.
    /// @param frame the samples to write.
    /// @return how many bytes the payload holds, or the armed failure.
    [[nodiscard]] Result<CodecEncodeReport> encode(const EngineContext&, const ExecutionContext&,
                                                   const CodecEncodeRequest& request, const RawFrame& frame,
                                                   const ImageMetadata&) const override
    {
        m_plan.encodes.fetch_add(1u, std::memory_order_relaxed);
        const auto fail = [this](std::uint64_t step) {
            if (m_plan.step != step)
            {
                return false;
            }
            m_plan.fired.store(true, std::memory_order_relaxed);
            return true;
        };
        if (fail(1u))
        {
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the encoder threw before it opened anything");
            }
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, request.format,
                                "an::faults::WritingCodec::encode", request.pendingPath);
        }

        const auto bytes = frame.bytes();
        std::uint64_t written = bytes.size();
        if (fail(2u))
        {
            written = bytes.size() / 2u;
        }
        std::ofstream out(std::filesystem::path(std::u8string_view(
                              reinterpret_cast<const char8_t*>(std::string(request.pendingPath).c_str()))),
                          std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return Status::failure(ErrorCode::ExternalFailure, "the pending path could not be opened");
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(written));
        out.close();

        if (fail(3u))
        {
            // The payload is whole and the report is short, which is the other side of a short write
            // and the one a length field in a header produces.
            return CodecEncodeReport{bytes.size() / 2u};
        }
        if (fail(4u))
        {
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the encoder threw with its bytes already on disk");
            }
            return Status::failure(ErrorCode::ExternalFailure,
                                   StatusDetail{.message = "no space left on device",
                                                .operation = "an::faults::WritingCodec::encode",
                                                .domain = ErrorDomain::Io});
        }
        if (fail(5u))
        {
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the encoder threw at its last step");
            }
            return Status::failure(ErrorCode::InvalidData, "the encoder refused the samples it was given");
        }
        return CodecEncodeReport{static_cast<std::uint64_t>(bytes.size())};
    }

private:
    /// What this codec was told to do. Borrowed.
    WritingCodecPlan& m_plan;
};

/// What a source, a sink or a processor was told to do.
struct StreamPlan
{
    /// How many frames the source holds.
    std::uint64_t frames = 12u;

    /// Which frame index the source fails at, or `kNever`.
    std::uint64_t failRead = kNever;

    /// Which frame index the processor fails at, or `kNever`.
    std::uint64_t failProcess = kNever;

    /// Which frame index the sink fails at, or `kNever`.
    std::uint64_t failWrite = kNever;

    /// Whether the sink fails when it is finished.
    bool failFinish = false;

    /// Whether the armed failure refuses or throws.
    FailureMode mode = FailureMode::Refuse;

    /// Whether the armed failure was reached.
    std::atomic<bool> fired{false};

    /// How many frames were written, which a successful run compares against.
    std::atomic<std::uint64_t> written{0u};

    /// An index no frame has.
    static constexpr std::uint64_t kNever = ~std::uint64_t{0};
};

/// A source of independent frames that can fail at a chosen one.
class CountingSource final : public FrameSourceBackend
{
public:
    /// A source under one plan.
    ///
    /// @param plan what to do. Borrowed; must outlive this source.
    explicit CountingSource(StreamPlan& plan) : m_plan(plan) {}

    /// Reads one frame, or fails when this is the armed index.
    ///
    /// @param engine where the samples come from.
    /// @param index which frame is wanted.
    /// @return the frame, or the armed failure.
    [[nodiscard]] Result<SequenceReadResult> read(const EngineContext& engine, const ExecutionContext&,
                                                  std::uint64_t index) override
    {
        if (index == m_plan.failRead)
        {
            m_plan.fired.store(true, std::memory_order_relaxed);
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the decoder threw on a frame");
            }
            return Status::failure(ErrorCode::InvalidData, "the source refused a frame");
        }
        Result<RawFrame> frame = makeFrame(engine, index);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        return SequenceReadResult{SequenceFrame{std::move(*frame), {}, {index}}};
    }

    /// Closes the source.
    ///
    /// @return success; this source owns no handle.
    [[nodiscard]] Status close() override
    {
        return Status::success();
    }

private:
    /// What this source was told to do. Borrowed.
    StreamPlan& m_plan;
};

/// A sink that counts what it was given and can fail at a chosen frame or at completion.
class CountingSink final : public FrameSinkBackend
{
public:
    /// A sink under one plan.
    ///
    /// @param plan what to do. Borrowed; must outlive this sink.
    explicit CountingSink(StreamPlan& plan) : m_plan(plan) {}

    /// Accepts one frame, or fails when this is the armed index.
    ///
    /// @param frame the frame being written.
    /// @return the cumulative counts, or the armed failure.
    [[nodiscard]] Result<SequenceWriteResult> append(const EngineContext&, const ExecutionContext&,
                                                     const SequenceFrame& frame) override
    {
        if (frame.position.index == m_plan.failWrite)
        {
            m_plan.fired.store(true, std::memory_order_relaxed);
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the encoder threw on a frame");
            }
            return Status::failure(ErrorCode::ExternalFailure, "the sink refused a frame");
        }
        const std::uint64_t count = m_plan.written.fetch_add(1u, std::memory_order_relaxed) + 1u;
        return SequenceWriteResult{count, count * kFrameBytes};
    }

    /// Completes the private output, or fails when that is what was armed.
    ///
    /// @return the final counts, or the armed failure.
    [[nodiscard]] Result<SequenceWriteResult> finish(const ExecutionContext&) override
    {
        if (m_plan.failFinish)
        {
            m_plan.fired.store(true, std::memory_order_relaxed);
            return Status::failure(ErrorCode::ExternalFailure, "the sink refused to complete its output");
        }
        const std::uint64_t count = m_plan.written.load(std::memory_order_relaxed);
        return SequenceWriteResult{count, count * kFrameBytes};
    }

    /// Releases what this sink holds.
    ///
    /// @return success; this sink owns no handle.
    [[nodiscard]] Status close() override
    {
        return Status::success();
    }

private:
    /// What this sink was told to do. Borrowed.
    StreamPlan& m_plan;
};

/// A processor that passes frames through and can fail at a chosen one.
class CountingProcessor final : public FrameStreamProcessor
{
public:
    /// A processor under one plan.
    ///
    /// @param plan what to do. Borrowed; must outlive this processor.
    explicit CountingProcessor(StreamPlan& plan) : m_plan(plan) {}

    /// Passes one frame through, or fails when this is the armed index.
    ///
    /// @param frame the frame being processed; ownership transfers in and out.
    /// @return the frame, or the armed failure.
    [[nodiscard]] Result<SequenceFrame> process(const EngineContext&, const ExecutionContext&,
                                                SequenceFrame frame) override
    {
        if (frame.position.index == m_plan.failProcess)
        {
            m_plan.fired.store(true, std::memory_order_relaxed);
            if (m_plan.mode == FailureMode::Throw)
            {
                throw std::runtime_error("the processor threw on a frame");
            }
            return Status::failure(ErrorCode::Unsupported, "the processor refused a frame");
        }
        return frame;
    }

private:
    /// What this processor was told to do. Borrowed.
    StreamPlan& m_plan;
};

/// The bounds every streaming scenario runs under: two frames in flight on each side, so a failure
/// happens with work queued behind it rather than with an empty queue.
///
/// @return the options.
[[nodiscard]] inline FrameStreamOptions streamBounds()
{
    return {.readAheadDepth = 2u,
            .writeBehindDepth = 2u,
            .maxFrameBytes = kFrameBytes,
            .readAheadBytes = 2u * kFrameBytes,
            .writeBehindBytes = 2u * kFrameBytes};
}

/// A registry holding this build's FITS adapter, for the scenarios whose subject is a real codec
/// reaching a real library.
///
/// @return the registry, or the failure that stopped it being built.
[[nodiscard]] inline Result<std::shared_ptr<const CodecRegistry>> fitsRegistry()
{
    Result<std::shared_ptr<const ImageCodec>> codec = createFitsCodec();
    if (!codec.hasValue())
    {
        return codec.status();
    }
    const std::array<std::shared_ptr<const ImageCodec>, 1u> codecs{*codec};
    return CodecRegistry::create(codecs);
}

/// A registry holding the writing codec, for the scenarios that make an encoder fail on purpose.
///
/// @param plan what that codec is to do. Borrowed by the codec; must outlive the registry.
/// @return the registry, or the failure that stopped it being built.
[[nodiscard]] inline Result<std::shared_ptr<const CodecRegistry>> writingRegistry(WritingCodecPlan& plan)
{
    const std::array<std::shared_ptr<const ImageCodec>, 1u> codecs{std::make_shared<const WritingCodec>(plan)};
    return CodecRegistry::create(codecs);
}

} // namespace an::faults
