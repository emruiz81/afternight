// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - turning decoded FFmpeg frames into engine frames, by a plan made once per stream.
//
// A stream's pixel format picks one path, and the delivery plan records it. Samples the engine can
// describe as stored - grey, a Bayer mosaic, packed RGB in red-first order, or the luma plane of YCbCr
// - are shared in place when the options let the decoder own them, and copied otherwise. RGB stored
// blue first or in planes is copied with its channels reordered, palette indices are looked up into
// RGB, and YCbCr, or an RGB packing the engine cannot describe, goes through the scaler into eight- or
// sixteen-bit RGB. Every refusal a stream can earn is decided in `planDelivery` when the source opens,
// not at its first frame.

#include "io/sequences/ffmpeg/FfmpegDelivery.h"

#include <AfterNightEngine/Runtime/TraceSink.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

namespace an::ffmpeg_detail
{
namespace
{

/// The grey ramp substituted for an empty palette: entry `i` is `i` in all three channels. Immutable
/// data, so concurrent sources share nothing that changes.
constexpr std::array<std::uint32_t, 256> kGrayRamp = [] {
    std::array<std::uint32_t, 256> ramp{};
    for (std::uint32_t entry = 0u; entry < 256u; ++entry)
        ramp[entry] = 0xFF000000u | (entry << 16u) | (entry << 8u) | entry;
    return ramp;
}();

/// A packed or planar RGB layout the engine describes as stored.
struct RgbLayout
{
    std::uint32_t channels = 3u;
    std::uint32_t elementBytes = 1u;
    bool planar = false;
    std::array<std::uint32_t, 4> sourcePlane{0u, 1u, 2u, 3u};
    bool exchange = false;
};

std::optional<RgbLayout> describeRgb(const AVPixFmtDescriptor& descriptor, bool floating)
{
    const int channels = descriptor.nb_components;
    const bool alpha = (descriptor.flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    if ((channels != 3 && channels != 4) || alpha != (channels == 4))
        return std::nullopt;
    const int depth = descriptor.comp[0].depth;
    const int elementBytes = floating ? (depth == 32 ? 4 : 0) : depth <= 8 ? 1 : depth <= 16 ? 2 : 0;
    if (elementBytes == 0)
        return std::nullopt;
    for (int component = 0; component < channels; ++component)
        if (descriptor.comp[component].depth != depth || descriptor.comp[component].shift != 0)
            return std::nullopt;
    RgbLayout layout;
    layout.channels = static_cast<std::uint32_t>(channels);
    layout.elementBytes = static_cast<std::uint32_t>(elementBytes);
    layout.planar = (descriptor.flags & AV_PIX_FMT_FLAG_PLANAR) != 0;
    if (layout.planar)
    {
        for (int component = 0; component < channels; ++component)
        {
            const AVComponentDescriptor& stored = descriptor.comp[component];
            if (stored.step != elementBytes || stored.offset != 0)
                return std::nullopt;
            for (int other = 0; other < component; ++other)
                if (descriptor.comp[other].plane == stored.plane)
                    return std::nullopt;
            layout.sourcePlane[static_cast<std::size_t>(component)] = static_cast<std::uint32_t>(stored.plane);
        }
        return layout;
    }
    if (depth != 8 * elementBytes)
        return std::nullopt;
    for (int component = 0; component < channels; ++component)
        if (descriptor.comp[component].plane != 0 || descriptor.comp[component].step != channels * elementBytes)
            return std::nullopt;
    if (descriptor.comp[1].offset != elementBytes || (channels == 4 && descriptor.comp[3].offset != 3 * elementBytes))
        return std::nullopt;
    if (descriptor.comp[0].offset == 0 && descriptor.comp[2].offset == 2 * elementBytes)
        return layout;
    if (descriptor.comp[0].offset == 2 * elementBytes && descriptor.comp[2].offset == 0)
    {
        layout.exchange = true;
        return layout;
    }
    return std::nullopt;
}

std::optional<CfaPattern> bayerPatternOf(std::string_view name) noexcept
{
    if (name.find("rggb") != std::string_view::npos)
        return CfaPattern::Rggb;
    if (name.find("bggr") != std::string_view::npos)
        return CfaPattern::Bggr;
    if (name.find("grbg") != std::string_view::npos)
        return CfaPattern::Grbg;
    if (name.find("gbrg") != std::string_view::npos)
        return CfaPattern::Gbrg;
    return std::nullopt;
}

Status unsupported(const char* message, const char* parameter, std::string_view path)
{
    return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, message, parameter, path);
}

Status beyondLimits(std::string_view path)
{
    return videoFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch,
                        "the stream's frames exceed the engine's limits", "extent", path);
}

/// The scaler's constant and the H.273 code for one YCbCr matrix; `matrixOf` answers nothing for a matrix
/// the adapter does not convert.
struct Matrix
{
    int scale;
    std::uint8_t code;
};

std::optional<Matrix> matrixOf(int colorSpace) noexcept
{
    switch (colorSpace)
    {
    case AVCOL_SPC_BT709:
        return Matrix{SWS_CS_ITU709, 1u};
    case AVCOL_SPC_FCC:
        return Matrix{SWS_CS_FCC, 4u};
    case AVCOL_SPC_BT470BG:
        return Matrix{SWS_CS_ITU601, 5u};
    case AVCOL_SPC_SMPTE170M:
        return Matrix{SWS_CS_ITU601, 6u};
    case AVCOL_SPC_SMPTE240M:
        return Matrix{SWS_CS_SMPTE240M, 7u};
    case AVCOL_SPC_BT2020_NCL:
        return Matrix{SWS_CS_BT2020, 9u};
    default:
        return std::nullopt;
    }
}

} // namespace

FfmpegColorRange colorRangeOf(int range) noexcept
{
    if (range == AVCOL_RANGE_MPEG)
        return FfmpegColorRange::Limited;
    if (range == AVCOL_RANGE_JPEG)
        return FfmpegColorRange::Full;
    return FfmpegColorRange::Unspecified;
}

const char* conversionName(FfmpegConversion conversion) noexcept
{
    switch (conversion)
    {
    case FfmpegConversion::None:
        return "None";
    case FfmpegConversion::ChannelReorder:
        return "ChannelReorder";
    case FfmpegConversion::PaletteExpansion:
        return "PaletteExpansion";
    case FfmpegConversion::LumaPlane:
        return "LumaPlane";
    case FfmpegConversion::ColorMatrix:
        return "ColorMatrix";
    case FfmpegConversion::Repack:
        return "Repack";
    }
    return "Unknown";
}

const char* colorRangeName(FfmpegColorRange range) noexcept
{
    switch (range)
    {
    case FfmpegColorRange::Unspecified:
        return "Unspecified";
    case FfmpegColorRange::Limited:
        return "Limited";
    case FfmpegColorRange::Full:
        return "Full";
    }
    return "Unknown";
}

Result<DeliveryPlan> planDelivery(const AVCodecParameters& parameters, const FfmpegSourceOptions& options,
                                  const EngineLimits& limits, double pixelAspectRatio, std::string_view path)
{
    const auto format = static_cast<AVPixelFormat>(parameters.format);
    const AVPixFmtDescriptor* descriptor = format == AV_PIX_FMT_NONE ? nullptr : av_pix_fmt_desc_get(format);
    if (descriptor == nullptr)
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "the stream states no pixel format", "pixelFormat", path);
    if (parameters.width <= 0 || parameters.height <= 0)
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "the stream states no frame size", parameters.width <= 0 ? "width" : "height", path);

    DeliveryPlan plan;
    plan.stored = format;
    plan.width = static_cast<std::uint32_t>(parameters.width);
    plan.height = static_cast<std::uint32_t>(parameters.height);
    int bits = 0;
    for (int component = 0; component < descriptor->nb_components; ++component)
        bits = (std::max)(bits, descriptor->comp[component].depth);
    plan.storedBits = static_cast<std::uint32_t>(bits);
    const bool bigEndian = (descriptor->flags & AV_PIX_FMT_FLAG_BE) != 0;
    const bool floating = (descriptor->flags & AV_PIX_FMT_FLAG_FLOAT) != 0;
    const bool alpha = (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    const std::uint32_t storedBytes = floating ? 4u : bits <= 8 ? 1u : 2u;
    const auto storedOrder = [bigEndian](std::uint32_t bytes) {
        return bytes == 1u ? ByteOrder::Native : bigEndian ? ByteOrder::Big : ByteOrder::Little;
    };
    plan.source =
        SourceEncoding{.sampleFormat = RawSampleFormat{storedBytes, floating ? RawSampleClass::FloatingPoint
                                                                             : RawSampleClass::UnsignedInteger},
                       .byteOrder = storedOrder(storedBytes)};
    const bool convert = options.sampleDelivery == FfmpegSampleDelivery::ConvertToRgb;

    ChannelSchema schema = ChannelSchema::Mono;
    std::uint32_t channels = 1u;
    std::uint32_t elementBytes = 1u;
    RawSampleClass sampleClass = RawSampleClass::UnsignedInteger;
    ByteOrder order = ByteOrder::Native;
    CfaDescriptor cfa{};
    bool planar = false;

    if ((descriptor->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM)) != 0)
        return unsupported("the pixel format does not hold whole samples", "pixelFormat", path);

    if ((descriptor->flags & AV_PIX_FMT_FLAG_BAYER) != 0)
    {
        const int step = descriptor->comp[0].step;
        const auto pattern = bayerPatternOf(descriptor->name);
        if ((step != 1 && step != 2) || !pattern)
            return unsupported("the mosaic layout has no description", "pixelFormat", path);
        elementBytes = static_cast<std::uint32_t>(step);
        order = storedOrder(elementBytes);
        cfa = *CfaDescriptor::create(*pattern, 0u, 0u);
        plan.shareable = true;
    }
    else if ((descriptor->flags & AV_PIX_FMT_FLAG_PAL) != 0)
    {
        if (!convert)
            return unsupported("palette indices are looked up only when the options convert to RGB", "pixelFormat",
                               path);
        schema = ChannelSchema::Rgb;
        channels = 3u;
        plan.conversion = FfmpegConversion::PaletteExpansion;
    }
    else if ((descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0)
    {
        if (const auto rgb = describeRgb(*descriptor, floating))
        {
            schema = rgb->channels == 4u ? ChannelSchema::Rgba : ChannelSchema::Rgb;
            channels = rgb->channels;
            elementBytes = rgb->elementBytes;
            sampleClass = floating ? RawSampleClass::FloatingPoint : RawSampleClass::UnsignedInteger;
            order = storedOrder(elementBytes);
            planar = rgb->planar;
            plan.sourcePlane = rgb->sourcePlane;
            plan.exchangeRedAndBlue = rgb->exchange;
            plan.conversion = rgb->planar || rgb->exchange ? FfmpegConversion::ChannelReorder : FfmpegConversion::None;
            plan.shareable = plan.conversion == FfmpegConversion::None;
        }
        else if (!convert)
            return unsupported("the RGB packing is repacked only when the options convert to RGB", "pixelFormat", path);
        else if (alpha)
            return unsupported("repacking the RGB packing would discard its alpha channel", "pixelFormat", path);
        else if (floating || sws_isSupportedInput(format) == 0)
            return unsupported("the RGB packing cannot be repacked", "pixelFormat", path);
        else
        {
            schema = ChannelSchema::Rgb;
            channels = 3u;
            elementBytes = bits <= 8 ? 1u : 2u;
            plan.conversion = FfmpegConversion::Repack;
        }
    }
    else if (descriptor->nb_components <= 2)
    {
        const AVComponentDescriptor& grey = descriptor->comp[0];
        const int step = floating ? 4 : grey.depth <= 8 ? 1 : 2;
        if (descriptor->nb_components == 2 || grey.plane != 0 || grey.offset != 0 || grey.shift != 0 ||
            grey.step != step || (floating && grey.depth != 32) || (!floating && grey.depth > 16))
            return unsupported("the grey layout has no description", "pixelFormat", path);
        elementBytes = static_cast<std::uint32_t>(step);
        sampleClass = floating ? RawSampleClass::FloatingPoint : RawSampleClass::UnsignedInteger;
        order = storedOrder(elementBytes);
        plan.shareable = true;
    }
    else
    {
        if ((descriptor->flags & AV_PIX_FMT_FLAG_XYZ) != 0)
            return unsupported("XYZ samples are neither described nor converted", "pixelFormat", path);
        if (options.sampleDelivery == FfmpegSampleDelivery::LumaPlane)
        {
            const AVComponentDescriptor& luma = descriptor->comp[0];
            if (floating || luma.plane != 0 || luma.offset != 0 || luma.shift != 0 ||
                luma.step != (luma.depth > 8 ? 2 : 1))
                return unsupported("the luma samples are not a plane of whole samples", "pixelFormat", path);
            elementBytes = static_cast<std::uint32_t>(luma.step);
            order = storedOrder(elementBytes);
            plan.storedBits = static_cast<std::uint32_t>(luma.depth);
            plan.conversion = FfmpegConversion::LumaPlane;
            plan.shareable = true;
        }
        else if (!convert)
            return unsupported("YCbCr samples are delivered only through a conversion", "pixelFormat", path);
        else
        {
            if (alpha)
                return unsupported("converting would discard the alpha channel", "pixelFormat", path);
            if (floating || sws_isSupportedInput(format) == 0)
                return unsupported("the YCbCr layout cannot be converted", "pixelFormat", path);
            std::optional<Matrix> matrix = matrixOf(parameters.color_space);
            if (parameters.color_space == AVCOL_SPC_UNSPECIFIED)
            {
                if (options.unspecifiedMatrix == FfmpegMatrixAssumption::Bt601)
                    matrix = Matrix{SWS_CS_ITU601, 6u};
                else if (options.unspecifiedMatrix == FfmpegMatrixAssumption::Bt709)
                    matrix = Matrix{SWS_CS_ITU709, 1u};
                else
                    return unsupported("the stream states no YCbCr matrix and the options assume none",
                                       "matrixCoefficients", path);
            }
            if (!matrix)
                return unsupported("the stream's YCbCr matrix is not one this adapter converts", "matrixCoefficients",
                                   path);
            FfmpegColorRange range = colorRangeOf(parameters.color_range);
            if (std::string_view(descriptor->name).starts_with("yuvj"))
                range = FfmpegColorRange::Full;
            if (range == FfmpegColorRange::Unspecified)
            {
                if (options.unspecifiedRange == FfmpegRangeAssumption::Limited)
                    range = FfmpegColorRange::Limited;
                else if (options.unspecifiedRange == FfmpegRangeAssumption::Full)
                    range = FfmpegColorRange::Full;
                else
                    return unsupported("the stream states no code range and the options assume none", "colorRange",
                                       path);
            }
            plan.scaleMatrix = matrix->scale;
            plan.conversionMatrix = matrix->code;
            plan.conversionRange = range;
            plan.sourceFullRange = range == FfmpegColorRange::Full ? 1 : 0;
            schema = ChannelSchema::Rgb;
            channels = 3u;
            elementBytes = bits <= 8 ? 1u : 2u;
            plan.conversion = FfmpegConversion::ColorMatrix;
        }
    }

    const bool converted = plan.conversion == FfmpegConversion::ColorMatrix ||
                           plan.conversion == FfmpegConversion::Repack ||
                           plan.conversion == FfmpegConversion::PaletteExpansion;
    const bool scaled = plan.conversion == FfmpegConversion::ColorMatrix || plan.conversion == FfmpegConversion::Repack;
    if (scaled)
    {
        plan.scaleTarget = elementBytes == 1u ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_RGB48;
        order = elementBytes == 1u ? ByteOrder::Native : nativeByteOrder();
    }
    const auto extent = ImageExtent::create(plan.width, plan.height, channels, limits);
    if (!extent)
        return beyondLimits(path);
    const ImageLayoutRequest request{*extent, elementBytes, planar ? 1u : channels, planar ? channels : 1u,
                                     scaled ? 64u : 1u};
    plan.layout = ImageLayout::create(request, limits);
    if (!plan.layout || plan.layout->rowStrideBytes() > static_cast<std::uint64_t>(INT_MAX))
        return beyondLimits(path);
    if (scaled)
    {
        // The scaler may write past the last sample of a row; rows are padded to 64 bytes and the
        // allocation carries one row more than the frame, so what it writes stays inside memory the
        // frame owns.
        const auto spare = ImageExtent::create(plan.width, plan.height + 1u, channels, limits);
        if (spare)
            plan.allocation = ImageLayout::create({*spare, elementBytes, channels, 1u, 64u}, limits);
        if (!plan.allocation)
            return beyondLimits(path);
    }

    const RawFrameDescriptorRequest frame{.extent = *extent,
                                          .sampleFormat = RawSampleFormat{elementBytes, sampleClass},
                                          .byteOrder = order,
                                          .channelSchema = schema,
                                          .colorSpace = converted ? ColorSpace::DisplayRgb : options.colorSpace,
                                          .numericDomain =
                                              converted ? NumericDomain::EncodedNonlinear : options.numericDomain,
                                          .cfa = cfa,
                                          .coordinateOrigin = CoordinateOrigin::TopLeft,
                                          .pixelAspectRatio = pixelAspectRatio};
    switch (RawFrameDescriptor::check(frame))
    {
    case DescriptorError::None:
        break;
    case DescriptorError::ColorSpaceMismatch:
        return videoFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch,
                            "the stated colour space cannot describe the stream's channels", "colorSpace", path);
    case DescriptorError::NumericDomainMismatch:
    case DescriptorError::NumericDomainNotRepresentable:
        return videoFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch,
                            "the stated numeric domain cannot describe the stream's samples", "numericDomain", path);
    default:
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "the stream's frames have no consistent description", "pixelFormat", path);
    }
    plan.frame = RawFrameDescriptor::create(frame);
    return plan;
}

FrameDelivery::FrameDelivery(DeliveryPlan plan) noexcept : m_plan(std::move(plan)) {}

Result<RawFrame> FrameDelivery::deliver(const EngineContext& engine, const ExecutionContext& execution,
                                        const AVFrame& frame, const FfmpegSourceOptions& options, std::string_view path)
{
    if (frame.format != m_plan.stored || frame.width != static_cast<int>(m_plan.width) ||
        frame.height != static_cast<int>(m_plan.height))
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "a decoded frame's size or pixel format differs from the stream's",
                            frame.format != m_plan.stored ? "pixelFormat" : "extent", path);
    switch (m_plan.conversion)
    {
    case FfmpegConversion::None:
    case FfmpegConversion::LumaPlane:
        if (options.frameMemory == FfmpegFrameMemory::DecoderOwned && m_plan.shareable)
        {
            auto shared = share(engine, execution, frame);
            if (!shared.hasValue() || shared->valid())
                return shared;
        }
        return copy(engine, execution, frame, path);
    case FfmpegConversion::ChannelReorder:
        return copy(engine, execution, frame, path);
    case FfmpegConversion::PaletteExpansion:
        return expandPalette(engine, execution, frame, options, path);
    case FfmpegConversion::ColorMatrix:
    case FfmpegConversion::Repack:
        return convert(engine, execution, frame, path);
    }
    return Status::failure(ErrorCode::Internal);
}

Result<RawFrame> FrameDelivery::share(const EngineContext& engine, const ExecutionContext& execution,
                                      const AVFrame& frame) const
{
    // A frame that cannot be described in place answers with an empty frame, and is copied instead.
    if (frame.linesize[0] <= 0 || frame.buf[0] == nullptr || frame.data[0] == nullptr)
        return RawFrame{};
    const ImageLayout& contiguous = *m_plan.layout;
    const auto stride = static_cast<std::uint64_t>(frame.linesize[0]);
    const auto layout = ImageLayout::createWithStrides(
        {contiguous.extent(), contiguous.elementSizeBytes(), contiguous.samplesPerPixel(), 1u, 1u}, stride,
        stride * m_plan.height, engine.limits());
    if (!layout)
        return RawFrame{};
    const std::uint64_t total = layout->totalBytes();
    const auto first = reinterpret_cast<std::uintptr_t>(frame.data[0]);
    const auto bufferFirst = reinterpret_cast<std::uintptr_t>(frame.buf[0]->data);
    const std::uint64_t bufferBytes = frame.buf[0]->size;
    if (first < bufferFirst || first - bufferFirst > bufferBytes || total > bufferBytes - (first - bufferFirst))
        return RawFrame{};
    AVFrame* reference = av_frame_clone(&frame);
    if (reference == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    std::shared_ptr<void> lifetime(reference, [](void* held) noexcept {
        auto* owned = static_cast<AVFrame*>(held);
        av_frame_free(&owned);
    });
    auto buffer = SampleBuffer::external(SampleBufferOwnership::DecoderOwned, reference->data[0], total,
                                         std::move(lifetime), false);
    if (!buffer.hasValue())
        return buffer.status();
    execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "bytes-shared", static_cast<double>(total), "bytes");
    return RawFrame::create(*m_plan.frame, *layout, std::move(*buffer));
}

Result<RawFrame> FrameDelivery::copy(const EngineContext& engine, const ExecutionContext& execution,
                                     const AVFrame& frame, std::string_view path) const
{
    const ImageLayout& layout = *m_plan.layout;
    auto buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    const std::span<std::byte> bytes = *buffer->mutableBytes();
    const std::uint64_t rowBytes = layout.minimumRowBytes();
    const std::size_t element = layout.elementSizeBytes();
    const std::size_t pixel = static_cast<std::size_t>(layout.samplesPerPixel()) * element;
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
    {
        const std::uint32_t source = m_plan.sourcePlane[plane];
        const std::uint8_t* base = frame.data[source];
        const std::int64_t stride = frame.linesize[source];
        if (base == nullptr || static_cast<std::uint64_t>(stride < 0 ? -stride : stride) < rowBytes)
            return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                                "a decoded frame's plane is shorter than its rows", "pixelFormat", path);
        for (std::uint32_t y = 0u; y < m_plan.height; ++y)
        {
            const std::uint8_t* from = base + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(stride);
            std::byte* const to = bytes.data() + static_cast<std::size_t>(*layout.rowByteOffset(plane, y));
            if (!m_plan.exchangeRedAndBlue)
            {
                std::memcpy(to, from, static_cast<std::size_t>(rowBytes));
                continue;
            }
            for (std::size_t offset = 0u; offset < rowBytes; offset += pixel)
            {
                std::memcpy(to + offset, from + offset + 2u * element, element);
                std::memcpy(to + offset + element, from + offset + element, element);
                std::memcpy(to + offset + 2u * element, from + offset, element);
                if (pixel == 4u * element)
                    std::memcpy(to + offset + 3u * element, from + offset + 3u * element, element);
            }
        }
    }
    execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "bytes-copied", static_cast<double>(layout.totalBytes()),
                          "bytes");
    return RawFrame::create(*m_plan.frame, layout, std::move(*buffer));
}

Result<RawFrame> FrameDelivery::expandPalette(const EngineContext& engine, const ExecutionContext& execution,
                                              const AVFrame& frame, const FfmpegSourceOptions& options,
                                              std::string_view path) const
{
    // A bottom-up DIB arrives flipped by the decoder: the first row pointer is the picture's top row and the
    // stride is negative, so the rows are walked with the signed stride.
    const std::int64_t stride = frame.linesize[0];
    if (frame.data[0] == nullptr || static_cast<std::uint64_t>(stride < 0 ? -stride : stride) < m_plan.width)
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "a decoded frame's plane is shorter than its rows", "pixelFormat", path);
    if (frame.data[1] == nullptr)
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                            "a decoded palette frame carries no palette", "palette", path);
    std::array<std::uint32_t, 256> palette{};
    std::memcpy(palette.data(), frame.data[1], sizeof(palette));
    // Empty means every entry is black whatever its alpha: FFmpeg's AVI reader makes every stored entry
    // opaque, so a palette the writer left zero arrives as 256 opaque blacks.
    if (std::all_of(palette.begin(), palette.end(), [](std::uint32_t entry) { return (entry & 0x00FFFFFFu) == 0u; }))
    {
        if (options.emptyPalette == FfmpegEmptyPalettePolicy::Refuse)
            return unsupported("the frame's palette is empty and the options state no substitute", "palette", path);
        palette = kGrayRamp;
    }
    const ImageLayout& layout = *m_plan.layout;
    auto buffer = SampleBuffer::createOwned(engine, layout, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    const std::span<std::byte> bytes = *buffer->mutableBytes();
    for (std::uint32_t y = 0u; y < m_plan.height; ++y)
    {
        const std::uint8_t* const from =
            frame.data[0] + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(stride);
        std::byte* const to = bytes.data() + static_cast<std::size_t>(*layout.rowByteOffset(0u, y));
        for (std::uint32_t x = 0u; x < m_plan.width; ++x)
        {
            const std::uint32_t entry = palette[from[x]];
            to[3u * x] = static_cast<std::byte>((entry >> 16u) & 0xFFu);
            to[3u * x + 1u] = static_cast<std::byte>((entry >> 8u) & 0xFFu);
            to[3u * x + 2u] = static_cast<std::byte>(entry & 0xFFu);
        }
    }
    execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "bytes-converted",
                          static_cast<double>(layout.totalBytes()), "bytes");
    return RawFrame::create(*m_plan.frame, layout, std::move(*buffer));
}

Result<RawFrame> FrameDelivery::convert(const EngineContext& engine, const ExecutionContext& execution,
                                        const AVFrame& frame, std::string_view path)
{
    const auto width = static_cast<int>(m_plan.width);
    const auto height = static_cast<int>(m_plan.height);
    if (!m_scale)
    {
        // Accurate rounding, full-resolution chroma on input and output, and bit-exact arithmetic, so the
        // result does not depend on which of the scaler's optimised paths the host selects. The output is
        // full-range RGB with neutral brightness, contrast and saturation.
        const int flags = SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP | SWS_BITEXACT;
        m_scale.reset(sws_getContext(width, height, m_plan.stored, width, height,
                                     static_cast<AVPixelFormat>(m_plan.scaleTarget), flags, nullptr, nullptr, nullptr));
        if (!m_scale)
            return unsupported("the scaler refused the conversion", "pixelFormat", path);
        if (m_plan.scaleMatrix >= 0 &&
            sws_setColorspaceDetails(m_scale.get(), sws_getCoefficients(m_plan.scaleMatrix), m_plan.sourceFullRange,
                                     sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16) < 0)
        {
            m_scale.reset();
            return unsupported("the scaler refused the colour matrix", "matrixCoefficients", path);
        }
    }
    const ImageLayout& layout = *m_plan.layout;
    auto buffer = SampleBuffer::createOwned(engine, *m_plan.allocation, StorageInitialization::Uninitialized);
    if (!buffer.hasValue())
        return buffer.status();
    const std::span<std::byte> bytes = *buffer->mutableBytes();
    std::array<std::uint8_t*, 4> targets{reinterpret_cast<std::uint8_t*>(bytes.data()), nullptr, nullptr, nullptr};
    const std::array<int, 4> strides{static_cast<int>(layout.rowStrideBytes()), 0, 0, 0};
    const int rows = sws_scale(m_scale.get(), frame.data, frame.linesize, 0, height, targets.data(), strides.data());
    if (rows != height)
        return videoFailure(ErrorCode::ExternalFailure, SequenceFailure::DecoderFailure,
                            "the scaler converted fewer rows than the frame holds", {}, path,
                            {StatusContextField::ofText(ContextKey::Library, "ffmpeg"),
                             StatusContextField::ofNumber(ContextKey::LibraryCode, rows)});
    execution.reportTrace(TraceEventKind::Counter, "ffmpeg", "bytes-converted",
                          static_cast<double>(layout.totalBytes()), "bytes");
    return RawFrame::create(*m_plan.frame, layout, std::move(*buffer));
}

} // namespace an::ffmpeg_detail
