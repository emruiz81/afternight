// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Video read through FFmpeg, one frame at a time, each saying what it is.
///
/// **A frame is described as the decoder produced it wherever the engine's vocabulary can say what it
/// is.** Monochrome and mosaic samples, packed and planar RGB, one or two bytes or a float per sample,
/// in the byte order the stream stored: those arrive unchanged apart from being gathered into one
/// buffer. Everything else - YCbCr, palettes, the RGB packings the vocabulary has no layout for - needs
/// arithmetic to become an image, and that arithmetic is a stated choice in `FfmpegSourceOptions`
/// rather than something done because it was possible. A YCbCr stream is converted through the matrix
/// and range the stream states; one that states neither is refused unless the caller says which to
/// assume.
///
/// **Index order is presentation order.** Opening a source reads every packet header of the chosen
/// stream once and builds an index sorted by presentation time, so an index names the picture a viewer
/// sees at that position, the count is exact, and a seek by index or by time lands exactly. Packets a
/// container marks as decoded but not shown - the frames an edit list trims - are decoded when they are
/// needed as references and never delivered.
///
/// **FFmpeg's objects never leave this adapter.** A frame either owns engine memory the adapter copied
/// into, or - when asked, and when the decoder's buffer is one run of bytes the engine can describe -
/// it holds a reference on the decoder's own buffer that is released when the last holder of the frame
/// lets it go, however long after the source closed that is.
///
/// **A device decodes only when asked, and delivers the processor's samples.** A source decodes on the processor
/// unless its options name a device, and a device is used only after it has decoded a frame of the stream. Its
/// frames are transferred to the processor in the device's own format and rearranged into the one the processor's
/// decoder produces, so they are delivered by the same plan as a frame the processor decoded.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FrameSource.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace an
{

/// Where a delivered frame's samples live.
enum class FfmpegFrameMemory : std::uint8_t
{
    /// Every frame is copied into memory from the engine context: counted against its budget, owned
    /// by the frame, and adoptable by an image with no further copy. The default.
    EngineOwned,

    /// A frame whose samples the decoder already holds as one run of bytes the engine can describe
    /// keeps a reference on the decoder's buffer instead of being copied, and states
    /// `SampleBufferOwnership::DecoderOwned`. Those bytes are not counted against the engine's budget
    /// and are released when the frame is. A frame that needs rearranging or converting, or whose
    /// decoder buffer is split across planes or runs bottom-up, is copied into engine memory exactly
    /// as under `EngineOwned`, and its buffer says so.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    DecoderOwned
};

/// What a source does with a stream whose samples the engine cannot describe as stored.
enum class FfmpegSampleDelivery : std::uint8_t
{
    /// YCbCr is converted to R'G'B' through the stream's matrix and range; palette indices are looked
    /// up; any other RGB packing is repacked. Eight-bit and narrower streams become eight-bit RGB, wider
    /// ones sixteen-bit RGB in host byte order. Streams the engine can describe as stored are not
    /// touched. The default.
    ConvertToRgb,

    /// A YCbCr stream delivers its luma plane as stored and nothing of its chroma; any other stream
    /// that needs arithmetic is refused with `SequenceFailure::UnsupportedEncoding`.
    LumaPlane,

    /// Only streams the engine can describe as stored are opened; anything that needs arithmetic is
    /// refused with `SequenceFailure::UnsupportedEncoding`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AsStoredOnly
};

/// Which YCbCr matrix a conversion uses when the stream states none.
enum class FfmpegMatrixAssumption : std::uint8_t
{
    /// A stream that states no matrix is refused with `SequenceFailure::UnsupportedEncoding` naming
    /// `matrixCoefficients`. The default.
    Refuse,

    /// ITU-R BT.601.
    Bt601,

    /// ITU-R BT.709.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bt709
};

/// Which code range a conversion uses when the stream states none.
enum class FfmpegRangeAssumption : std::uint8_t
{
    /// A stream that states no range is refused with `SequenceFailure::UnsupportedEncoding` naming
    /// `colorRange`. The default.
    Refuse,

    /// Luma from 16 to 235 and chroma from 16 to 240 at eight bits, scaled for wider samples.
    Limited,

    /// Every code of the sample width.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Full
};

/// What a source does with a palette frame whose 256 entries all have zero red, green and blue, whatever
/// their alpha - what a paletted AVI written without a palette holds.
enum class FfmpegEmptyPalettePolicy : std::uint8_t
{
    /// The read fails with `SequenceFailure::UnsupportedEncoding` naming `palette`. The default.
    Refuse,

    /// Each index is looked up in the grey ramp whose entry `i` has all three channels equal to `i`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Grayscale
};

/// Which device decodes a stream.
///
/// A device decodes into its own memory. Each frame is transferred to the processor in the device's own format
/// and rearranged, without changing a sample, into the format the processor's decoder produces, so a delivered frame is
/// the one the processor would deliver for every coding whose standard fixes its decoded samples - H.264, H.265,
/// VP9 and AV1 among them - and may differ in a coding whose standard leaves the inverse transform to the decoder,
/// as MPEG-2 and MPEG-4 Part 2 do.
enum class FfmpegDecodeDevice : std::uint8_t
{
    /// FFmpeg's software decoder on the processor. The default.
    Processor,

    /// The first of CUDA, Direct3D 11, VA-API and VideoToolbox, tried in that order, that decodes a frame
    /// of the stream; the processor when none does.
    Automatic,

    /// NVIDIA's CUDA video decoder, or the processor when it cannot decode the stream.
    Cuda,

    /// Direct3D 11 video acceleration, or the processor when it cannot decode the stream.
    D3d11va,

    /// VA-API, or the processor when it cannot decode the stream.
    Vaapi,

    /// VideoToolbox, or the processor when it cannot decode the stream.
    VideoToolbox,

    /// Vulkan video, or the processor when it cannot decode the stream.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Vulkan
};

/// The stable name of a decode device, as a frame's card carries it.
///
/// Thread-safe and allocation-free.
///
/// @param device the device.
/// @return `processor`, `auto`, `cuda`, `d3d11va`, `vaapi`, `videotoolbox` or `vulkan`; `unknown` for a value
///         outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* ffmpegDecodeDeviceName(FfmpegDecodeDevice device) noexcept;

/// A stream's code range, as H.273 names it.
enum class FfmpegColorRange : std::uint8_t
{
    /// The stream does not say.
    Unspecified,

    /// Limited ("studio" or "TV") range.
    Limited,

    /// Every code of the sample width.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Full
};

/// What a source does to reach the samples it delivers. One value holds for every frame of a stream.
enum class FfmpegConversion : std::uint8_t
{
    /// The samples are delivered as the decoder produced them.
    None,

    /// Blue-first packed RGB or green-first planar RGB is rearranged into red, green and blue. Samples
    /// are moved, never changed.
    ChannelReorder,

    /// Palette indices are looked up into eight-bit RGB. The palette's alpha is not delivered.
    PaletteExpansion,

    /// The luma plane of a YCbCr stream is delivered as stored; chroma is not delivered.
    LumaPlane,

    /// YCbCr is converted to R'G'B' through `FfmpegStreamInfo::conversionMatrix` and
    /// `FfmpegStreamInfo::conversionRange`.
    ColorMatrix,

    /// Another RGB packing is repacked into eight- or sixteen-bit RGB.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Repack
};

/// How a source reads a video. Copied by every entry point that takes one.
struct FfmpegSourceOptions
{
    /// Where delivered samples live.
    FfmpegFrameMemory frameMemory = FfmpegFrameMemory::EngineOwned;

    /// What happens to a stream the engine cannot describe as stored.
    FfmpegSampleDelivery sampleDelivery = FfmpegSampleDelivery::ConvertToRgb;

    /// The matrix a YCbCr conversion assumes when the stream states none.
    FfmpegMatrixAssumption unspecifiedMatrix = FfmpegMatrixAssumption::Refuse;

    /// The range a YCbCr conversion assumes when the stream states none.
    FfmpegRangeAssumption unspecifiedRange = FfmpegRangeAssumption::Refuse;

    /// What an all-zero palette means.
    FfmpegEmptyPalettePolicy emptyPalette = FfmpegEmptyPalettePolicy::Refuse;

    /// Which stream to read. Absent reads the first video stream that is not an attached picture.
    std::optional<std::uint32_t> streamIndex{};

    /// The basis of the channels of a stream delivered as stored. A video container rarely says, so
    /// the default claims none; a mono or mosaic stream admits nothing else. A converted stream is
    /// always display-referred RGB, which the conversion itself establishes, and ignores this.
    ColorSpace colorSpace = ColorSpace::Unspecified;

    /// What the numbers of a stream delivered as stored mean. The default is a sensor code, which is
    /// what an astronomical recording holds; a physically scaled domain is refused for integer
    /// samples. A converted stream is always nonlinearly encoded and ignores this.
    NumericDomain numericDomain = NumericDomain::RawSensorCode;

    /// Which device decodes the stream. A named device, or each device `FfmpegDecodeDevice::Automatic`
    /// tries, must open, offer a decoder for the stream's coding and decode a frame of the stream through a
    /// second reader of the file; otherwise the processor decodes.
    FfmpegDecodeDevice decodeDevice = FfmpegDecodeDevice::Processor;
};

/// What a source makes of one stream of a video under a set of options.
///
/// Built only by `inspectFfmpegFile`, so every value is consistent with the source
/// `createFfmpegSource` opens over the same file with the same options.
struct FfmpegStreamInfo
{
    /// `SequenceFormat::Mp4` for the MP4 family, `SequenceFormat::Avi` for AVI and
    /// `SequenceFormat::Video` for every other container.
    SequenceFormat format;

    /// FFmpeg's short name for the container, such as `matroska,webm`. Reported, never interpreted.
    std::string containerName;

    /// FFmpeg's short name for the stream's codec, such as `ffv1`. Reported, never interpreted.
    std::string codecName;

    /// FFmpeg's name for the pixel format the decoder produces, such as `yuv420p10le`.
    std::string storedPixelFormat;

    /// The stream read.
    std::uint32_t streamIndex;

    /// The widest component the stored pixel format holds, in bits.
    std::uint32_t storedBitsPerComponent;

    /// The stream's matrix coefficients as an H.273 code point; 2 when it states none.
    std::uint8_t matrixCoefficients;

    /// The stream's colour primaries as an H.273 code point; 2 when it states none.
    std::uint8_t colourPrimaries;

    /// The stream's transfer characteristics as an H.273 code point; 2 when it states none.
    std::uint8_t transferCharacteristics;

    /// The stream's code range as it states it.
    FfmpegColorRange colorRange;

    /// What the source does to reach the delivered samples.
    FfmpegConversion conversion;

    /// The matrix a `FfmpegConversion::ColorMatrix` conversion applies, as an H.273 code point, which is
    /// the stream's own or the assumed one; 2 for every other conversion.
    std::uint8_t conversionMatrix;

    /// The range a `FfmpegConversion::ColorMatrix` conversion reads, the stream's own or the assumed
    /// one; `FfmpegColorRange::Unspecified` for every other conversion.
    FfmpegColorRange conversionRange;

    /// Every delivered frame's description.
    RawFrameDescriptor frame;

    /// How many frames a source delivers: the stream's packets less the ones the container marks as not
    /// shown and any shown before the first keyframe, which nothing can decode.
    std::uint64_t frameCount;

    /// How many of the stream's packets are keyframes.
    std::uint64_t keyframeCount;

    /// How many packets are decoded when they are needed and never delivered.
    std::uint64_t hiddenPacketCount;

    /// Seconds per tick of the stream's timestamps, which is the source's time base.
    SequenceRate timeBase;

    /// The stream's average frame rate, when it states one. Informational.
    std::optional<SequenceRate> nominalFrameRate;

    /// The exclusive end of the timeline in ticks from the first frame, when every frame states its
    /// duration.
    std::optional<std::int64_t> durationTicks;

    /// Whether any frame is shown in a different order than it is decoded.
    bool reordered;

    /// Whether the decoder's independent slices run on the reading operation's threads. True only for
    /// decoders whose slices this adapter has established are independent, and never for a device.
    bool slicesOnOperationThreads;

    /// The device that decodes the stream: the processor, or the device the options named or
    /// `FfmpegDecodeDevice::Automatic` found. Never `FfmpegDecodeDevice::Automatic` itself.
    FfmpegDecodeDevice decodeDevice;
};

/// Judges a video file and indexes the chosen stream, without opening a session or decoding a frame.
///
/// Refuses exactly what `createFfmpegSource` refuses under the same options. Reads every packet of the stream once; the
/// index is held in memory from the engine context while it is built and released before return. When the options name
/// a device, it is opened and decodes a frame of the stream through a second reader of the file, and both are released
/// before return; the device reported is the one a source opened over the same file with the same options decodes on,
/// unless the device's availability changes between the two. The container is read through the engine's own file
/// boundary: FFmpeg opens no file and no network resource by name, and a container that asks to open another resource
/// is refused.
///
/// @param engine the context whose limits apply and whose memory holds the index while it is built.
/// @param execution an operation of that context; its cancellation is checked between reads, inside
///        FFmpeg's own reads, and between packets.
/// @param path nonempty UTF-8 without NUL; borrowed for this call.
/// @param options how the video is to be read.
/// @return the summary; `ErrorCode::InvalidArgument` naming the path or an option outside its
///         enumeration; `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure` carrying the path
///         and the operating system's code; `ErrorCode::IncompatibleArguments` with
///         `SequenceFailure::DescriptorMismatch` naming `streamIndex` for a stream that is not a video
///         or does not exist, or naming `colorSpace` or `numericDomain` for a stated meaning the
///         frames cannot have; `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`
///         naming `streamIndex` for a container with no video, `codec` for a stream no decoder in
///         this build reads, or `pixelFormat`, `matrixCoefficients` or `colorRange` for what the
///         options do not admit; `ErrorCode::InvalidData` with `SequenceFailure::MalformedContainer`
///         naming the field, or with `SequenceFailure::InvalidTimestamps` for a stream whose packets
///         cannot be put in presentation order; `ErrorCode::LimitExceeded` with
///         `SequenceFailure::DescriptorMismatch` for frames beyond the engine's limits;
///         `ErrorCode::ExternalFailure` with `SequenceFailure::DecoderFailure` carrying FFmpeg's code
///         for anything else FFmpeg reports; a memory-budget refusal; or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FfmpegStreamInfo> inspectFfmpegFile(const EngineContext& engine,
                                                                              const ExecutionContext& execution,
                                                                              std::string_view path,
                                                                              const FfmpegSourceOptions& options);

/// Opens one video stream as an index- and time-seekable source of its frames in presentation order.
///
/// The file stays open, shared for reading, writing and deletion by other handles, until the source
/// closes. The index of the stream's packets is built as `inspectFfmpegFile` builds it and held in
/// memory from the engine context for the life of the source. A read decodes forward from the decoder's
/// current position when the frame asked for is reachable from there, and otherwise repositions the
/// demuxer on the last keyframe from which it is decodable, checking where the demuxer actually landed;
/// frames decoded on the way are discarded and counted. The decoder starts no thread of its own: it
/// runs on the reading thread and, for decoders whose slices are independent, runs those slices on the
/// reading operation's admitted threads.
///
/// Every frame carries the stream's technical facts as `CodecNamespace::Ffmpeg` cards, the device that
/// decoded it among them as `decodeDevice`, and its stored sample width and byte order as its source encoding;
/// it never carries an output intent. A device is chosen when the source opens and for its whole life: a read
/// the device can no longer decode or transfer fails rather than falling back to the processor. Presentation
/// ticks count the stream's time base from the first frame; a frame's duration is stated when its packet
/// states one. Cancellation is checked between packets and inside FFmpeg's reads, and each read reports
/// `ffmpeg` trace counters for packets sent, frames decoded, frames discarded, repositions, bytes copied
/// and bytes converted.
///
/// @param engine the context owning the session and every engine-owned frame allocation.
/// @param execution an operation of that context, borrowed for this call.
/// @param path nonempty UTF-8 without NUL; borrowed for this call.
/// @param options how the video is read; copied.
/// @return an open source, or what `inspectFfmpegFile` refuses. A read that meets a packet the decoder
///         refuses fails with `SequenceFailure::DecoderFailure`, one whose frame never appears or
///         changes geometry with `SequenceFailure::MalformedContainer`, and one that meets an empty
///         palette under the default policy with `SequenceFailure::UnsupportedEncoding`, each beneath
///         the session's own failure, and publishes no frame; a read the device cannot decode or transfer
///         fails with `SequenceFailure::DecoderFailure` carrying FFmpeg's code.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSource> createFfmpegSource(const EngineContext& engine,
                                                                          const ExecutionContext& execution,
                                                                          std::string_view path,
                                                                          const FfmpegSourceOptions& options);

} // namespace an
