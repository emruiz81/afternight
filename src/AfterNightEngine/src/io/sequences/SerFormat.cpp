// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - decoding and judging a SER container's header, and reading its timestamp trailer.
//
// Every header field is decoded byte by byte as little-endian, so the reading does not depend on the
// host. The frame count is judged against the payload, and the options decide whether a count the
// payload contradicts is refused or replaced by the number of whole frames the payload holds; a
// container whose count was replaced reads no trailer, and a partial last frame is never exposed. A
// trailer is read into engine-context memory in bounded transfers with a cancellation check
// before each, and its ticks become the timeline only when every one names an instant and none
// decreases.

#include "io/sequences/SerFormat.h"

#include <AfterNightEngine/Runtime/TraceSink.h>

#include "core/Utf8.h"
#include "io/sequences/SequenceInternal.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace an::ser_detail
{
namespace
{
constexpr std::size_t kTicksPerTransfer = static_cast<std::size_t>(kTransferBytes / 8u);

SerHeader decodeHeader(std::span<const std::byte> bytes)
{
    SerHeader header;
    header.fileId = decodeText(bytes.subspan(0u, kFileIdBytes));
    header.luId = static_cast<std::int32_t>(readLe32(bytes, kLuIdOffset));
    header.colorId = static_cast<std::int32_t>(readLe32(bytes, kColorIdOffset));
    header.littleEndian = static_cast<std::int32_t>(readLe32(bytes, kLittleEndianOffset));
    header.imageWidth = static_cast<std::int32_t>(readLe32(bytes, kWidthOffset));
    header.imageHeight = static_cast<std::int32_t>(readLe32(bytes, kHeightOffset));
    header.pixelDepthPerPlane = static_cast<std::int32_t>(readLe32(bytes, kDepthOffset));
    header.frameCount = readLe32(bytes, kFrameCountOffset);
    header.observer = decodeText(bytes.subspan(kObserverOffset, kTextBytes));
    header.instrument = decodeText(bytes.subspan(kInstrumentOffset, kTextBytes));
    header.telescope = decodeText(bytes.subspan(kTelescopeOffset, kTextBytes));
    header.dateTimeTicks = readLe64(bytes, kDateTimeOffset);
    header.dateTimeUtcTicks = readLe64(bytes, kDateTimeUtcOffset);
    return header;
}

/// The bytes a container needs for a count of frames, saturating rather than wrapping.
std::uint64_t requiredBytes(std::uint64_t frames, std::uint64_t frameBytes) noexcept
{
    const std::uint64_t room = (std::numeric_limits<std::uint64_t>::max)() - kSerHeaderBytes;
    if (frames != 0u && frameBytes > room / frames)
        return (std::numeric_limits<std::uint64_t>::max)();
    return kSerHeaderBytes + frames * frameBytes;
}

const char* descriptorParameter(DescriptorError error) noexcept
{
    switch (error)
    {
    case DescriptorError::ColorSpaceMismatch:
        return "colorSpace";
    case DescriptorError::NumericDomainMismatch:
    case DescriptorError::NumericDomainNotRepresentable:
        return "numericDomain";
    default:
        return "descriptor";
    }
}
} // namespace

std::optional<ColorLayout> colorLayoutOf(std::int32_t colorId) noexcept
{
    switch (colorId)
    {
    case 0:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::None, 1u, false, false};
    case 8:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::Rggb, 1u, false, false};
    case 9:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::Grbg, 1u, false, false};
    case 10:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::Gbrg, 1u, false, false};
    case 11:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::Bggr, 1u, false, false};
    case 16:
    case 17:
    case 18:
    case 19:
        return ColorLayout{ChannelSchema::Mono, CfaPattern::None, 1u, false, true};
    case 100:
        return ColorLayout{ChannelSchema::Rgb, CfaPattern::None, 3u, false, false};
    case 101:
        return ColorLayout{ChannelSchema::Rgb, CfaPattern::None, 3u, true, false};
    default:
        return std::nullopt;
    }
}

std::optional<std::int32_t> colorIdOf(const RawFrameDescriptor& frame) noexcept
{
    const auto& cfa = frame.cfa();
    if (frame.channelSchema() == ChannelSchema::Rgb && frame.extent().channelCount() == 3u && !cfa.isMosaiced())
        return 100;
    if (frame.channelSchema() != ChannelSchema::Mono || frame.extent().channelCount() != 1u)
        return std::nullopt;
    if (!cfa.isMosaiced())
        return 0;
    if (cfa.hasFilterTable())
        return std::nullopt;
    // A phase moves which filter the first pixel is under, and each phase of a two-by-two mosaic is
    // one of the four named patterns - so the stored name is read off the first block.
    const auto startsWith = [&](CfaColor first, CfaColor second, CfaColor third, CfaColor fourth) {
        return cfa.colorAt(0u, 0u) == first && cfa.colorAt(1u, 0u) == second && cfa.colorAt(0u, 1u) == third &&
               cfa.colorAt(1u, 1u) == fourth;
    };
    if (startsWith(CfaColor::Red, CfaColor::Green, CfaColor::Green, CfaColor::Blue))
        return 8;
    if (startsWith(CfaColor::Green, CfaColor::Red, CfaColor::Blue, CfaColor::Green))
        return 9;
    if (startsWith(CfaColor::Green, CfaColor::Blue, CfaColor::Red, CfaColor::Green))
        return 10;
    if (startsWith(CfaColor::Blue, CfaColor::Green, CfaColor::Green, CfaColor::Red))
        return 11;
    return std::nullopt;
}

bool storesBigEndian(std::int32_t flag, SerEndianConvention convention) noexcept
{
    return convention == SerEndianConvention::Compatible ? flag == 1 : flag == 0;
}

std::uint32_t readLe32(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    std::uint32_t value = 0u;
    for (std::size_t index = 0u; index < 4u; ++index)
        value |= std::uint32_t{std::to_integer<std::uint8_t>(bytes[offset + index])} << (8u * index);
    return value;
}

std::uint64_t readLe64(std::span<const std::byte> bytes, std::size_t offset) noexcept
{
    std::uint64_t value = 0u;
    for (std::size_t index = 0u; index < 8u; ++index)
        value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[offset + index])} << (8u * index);
    return value;
}

void writeLe32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept
{
    for (std::size_t index = 0u; index < 4u; ++index)
        bytes[offset + index] = static_cast<std::byte>((value >> (8u * index)) & 0xffu);
}

void writeLe64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept
{
    for (std::size_t index = 0u; index < 8u; ++index)
        bytes[offset + index] = static_cast<std::byte>((value >> (8u * index)) & 0xffu);
}

std::string decodeText(std::span<const std::byte> field)
{
    std::size_t length = 0u;
    while (length < field.size() && field[length] != std::byte{0})
        ++length;
    while (length > 0u && field[length - 1u] == std::byte{0x20})
        --length;
    const std::string_view raw(reinterpret_cast<const char*>(field.data()), length);
    if (isValidUtf8WithoutNul(raw))
        return std::string(raw);
    // Writers of this format store single-byte text, so a byte that is not UTF-8 is read as Latin-1:
    // every byte maps to one code point and nothing the file held is dropped.
    std::string text;
    text.reserve(length * 2u);
    for (const char character : raw)
    {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x80u)
        {
            text.push_back(character);
            continue;
        }
        text.push_back(static_cast<char>(0xc0u | (value >> 6u)));
        text.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
    }
    return text;
}

Status serFailure(ErrorCode code, SequenceFailure reason, const char* message, std::string_view parameter,
                  std::string_view path, std::vector<StatusContextField> extra, Status cause)
{
    std::vector<StatusContextField> context;
    context.reserve(3u + extra.size());
    context.push_back(StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason)));
    if (!parameter.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(parameter)));
    if (!path.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Path, std::string(path)));
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(
        code, {.message = message, .operation = "ser", .domain = ErrorDomain::Io, .context = std::move(context)},
        std::move(cause));
}

Status argumentFailure(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = "ser",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status ioFailure(int platformError, const char* message, std::string_view path)
{
    return serFailure(ErrorCode::ExternalFailure, SequenceFailure::IoFailure, message, {}, path,
                      {StatusContextField::ofNumber(ContextKey::LibraryCode, platformError)});
}

Status checkOptions(const SerSourceOptions& options)
{
    if (static_cast<std::uint8_t>(options.endianConvention) >
        static_cast<std::uint8_t>(SerEndianConvention::Specification))
        return argumentFailure("the byte-order convention is not one this build knows", "endianConvention");
    if (static_cast<std::uint8_t>(options.frameCountPolicy) >
        static_cast<std::uint8_t>(SerFrameCountPolicy::CompleteFrames))
        return argumentFailure("the frame-count policy is not one this build knows", "frameCountPolicy");
    if (static_cast<std::uint8_t>(options.timelinePolicy) > static_cast<std::uint8_t>(SerTimelinePolicy::Require))
        return argumentFailure("the timeline policy is not one this build knows", "timelinePolicy");
    if (static_cast<std::uint8_t>(options.cmyMosaicPolicy) >
        static_cast<std::uint8_t>(SerCmyMosaicPolicy::DescribeAsMono))
        return argumentFailure("the CMY mosaic policy is not one this build knows", "cmyMosaicPolicy");
    if (!isColorSpace(options.colorSpace))
        return argumentFailure("the colour space is not one this build knows", "colorSpace");
    if (!isNumericDomain(options.numericDomain))
        return argumentFailure("the numeric domain is not one this build knows", "numericDomain");
    return Status::success();
}

Result<SerContainer> inspect(std::span<const std::byte> header, std::uint64_t fileBytes,
                             const SerSourceOptions& options, const EngineLimits& limits, std::string_view path)
{
    if (header.size() < kSerHeaderBytes)
        return argumentFailure("a SER header span holds at least the fixed header", "header");
    if (const Status checked = checkOptions(options); checked.failed())
        return checked;
    if (fileBytes < kSerHeaderBytes)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::TruncatedContainer,
                          "the container is shorter than the SER header", {}, path,
                          {StatusContextField::ofCount(ContextKey::Requested, kSerHeaderBytes),
                           StatusContextField::ofCount(ContextKey::Available, fileBytes)});
    SerHeader decoded = decodeHeader(header.first(kSerHeaderBytes));
    const std::optional<ColorLayout> color = colorLayoutOf(decoded.colorId);
    if (!color)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                          "the colour identifier is not one SER defines", "ColorID", path);
    if (color->cmy && options.cmyMosaicPolicy == SerCmyMosaicPolicy::Refuse)
        return serFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                          "a CMY mosaic cannot be described by the filter-array vocabulary", "ColorID", path);
    if (decoded.pixelDepthPerPlane < 1 || decoded.pixelDepthPerPlane > 16)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                          "the depth is outside one to sixteen bits", "PixelDepthPerPlane", path);
    if (decoded.imageWidth <= 0)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer, "the width is not positive",
                          "ImageWidth", path);
    if (decoded.imageHeight <= 0)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer, "the height is not positive",
                          "ImageHeight", path);
    const std::uint32_t sampleBytes = decoded.pixelDepthPerPlane > 8 ? 2u : 1u;
    const auto extent = ImageExtent::create(static_cast<std::uint32_t>(decoded.imageWidth),
                                            static_cast<std::uint32_t>(decoded.imageHeight), color->channels, limits);
    const auto layout = extent ? ImageLayout::create({*extent, sampleBytes, color->channels, 1u, 1u}, limits)
                               : std::optional<ImageLayout>{};
    if (!layout)
        return serFailure(ErrorCode::LimitExceeded, SequenceFailure::DescriptorMismatch,
                          "the frames exceed the engine's limits", "extent", path);
    const ByteOrder byteOrder = sampleBytes == 1u                                                 ? ByteOrder::Native
                                : storesBigEndian(decoded.littleEndian, options.endianConvention) ? ByteOrder::Big
                                                                                                  : ByteOrder::Little;
    const RawFrameDescriptorRequest request{.extent = *extent,
                                            .sampleFormat = {sampleBytes, RawSampleClass::UnsignedInteger},
                                            .byteOrder = byteOrder,
                                            .channelSchema = color->schema,
                                            .colorSpace = options.colorSpace,
                                            .numericDomain = options.numericDomain,
                                            .cfa = *CfaDescriptor::create(color->pattern, 0u, 0u),
                                            .coordinateOrigin = CoordinateOrigin::TopLeft,
                                            .pixelAspectRatio = 1.0};
    if (const DescriptorError error = RawFrameDescriptor::check(request); error != DescriptorError::None)
        return serFailure(ErrorCode::IncompatibleArguments, SequenceFailure::DescriptorMismatch,
                          "the stated meaning cannot describe this container's frames", descriptorParameter(error),
                          path);
    const std::uint64_t frameBytes = layout->totalBytes();
    const std::uint64_t payloadBytes = fileBytes - kSerHeaderBytes;
    const std::uint64_t capacity = payloadBytes / frameBytes;
    std::uint64_t frameCount = decoded.frameCount;
    bool recovered = false;
    if (frameCount > capacity || (frameCount == 0u && capacity > 0u))
    {
        if (options.frameCountPolicy == SerFrameCountPolicy::CompleteFrames)
        {
            recovered = frameCount != capacity;
            frameCount = capacity;
        }
        else if (frameCount > capacity)
            return serFailure(
                ErrorCode::InvalidData, SequenceFailure::TruncatedContainer,
                "the payload holds fewer frames than the header counts", "FrameCount", path,
                {StatusContextField::ofCount(ContextKey::Requested, requiredBytes(frameCount, frameBytes)),
                 StatusContextField::ofCount(ContextKey::Available, fileBytes)});
        else
            return serFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer,
                              "the header counts no frames over a payload that holds some", "FrameCount", path,
                              {StatusContextField::ofCount(ContextKey::Available, capacity)});
    }
    const std::uint64_t remaining = payloadBytes - frameCount * frameBytes;
    const bool trailer = !recovered && frameCount > 0u && remaining / 8u >= frameCount;
    const std::uint64_t trailing = remaining - (trailer ? 8u * frameCount : 0u);
    return SerContainer{.header = std::move(decoded),
                        .frame = *RawFrameDescriptor::create(request),
                        .layout = *layout,
                        .frameCount = frameCount,
                        .fileBytes = fileBytes,
                        .trailingBytes = trailing,
                        .trailerPresent = trailer,
                        .frameCountRecovered = recovered,
                        .channelOrderExchanged = color->exchanged};
}

TimelineSummary summarize(std::span<const std::uint64_t> ticks) noexcept
{
    TimelineSummary summary;
    if (ticks.empty())
        return summary;
    bool ordered = true;
    std::uint64_t minimum = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t maximum = 0u;
    for (std::size_t index = 0u; index < ticks.size(); ++index)
    {
        if (!utcInstantFromSerTicks(ticks[index]))
        {
            ordered = false;
            continue;
        }
        if (index > 0u && ticks[index] < ticks[index - 1u])
            ordered = false;
        minimum = (std::min)(minimum, ticks[index]);
        maximum = (std::max)(maximum, ticks[index]);
        ++summary.present;
    }
    summary.ordered = ordered;
    summary.first = utcInstantFromSerTicks(ticks.front());
    summary.last = utcInstantFromSerTicks(ticks.back());
    if (summary.present >= 2u && maximum > minimum)
    {
        // The span's ticks are tenths of a microsecond, taken in thousands before the division.
        const double difference = static_cast<double>(maximum - minimum) / 1000.0;
        summary.frameRate = static_cast<double>(summary.present - 1u) * 10000.0 / difference;
    }
    return summary;
}

Result<OpenedContainer> open(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                             const SerSourceOptions& options)
{
    if (!engine.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (const Status checked = sequence_detail::checkContext(engine.id(), execution); checked.failed())
        return checked;
    if (!isBoundaryPath(path))
        return argumentFailure("a SER path is nonempty bounded UTF-8 without NUL", "path");
    if (const Status checked = checkOptions(options); checked.failed())
        return checked;
    SequenceFile file;
    const std::filesystem::path native(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (const int error = file.openForReading(native); error != 0)
        return ioFailure(error, "the SER container could not be opened", path);
    std::uint64_t fileBytes = 0u;
    if (const int error = file.size(fileBytes); error != 0)
        return ioFailure(error, "the SER container's size could not be read", path);
    HeaderBytes header{};
    if (fileBytes >= kSerHeaderBytes)
    {
        std::size_t received = 0u;
        if (const int error = file.readAt(header.data(), header.size(), 0u, received); error != 0)
            return ioFailure(error, "the SER header could not be read", path);
        if (received != header.size())
            return serFailure(ErrorCode::InvalidData, SequenceFailure::TruncatedContainer,
                              "the container ended inside its header", {}, path,
                              {StatusContextField::ofCount(ContextKey::Requested, kSerHeaderBytes),
                               StatusContextField::ofCount(ContextKey::Available, received)});
    }
    auto container = inspect(header, fileBytes, options, engine.limits(), path);
    if (!container.hasValue())
        return container.status();
    AccountedBlock ticks;
    std::span<const std::uint64_t> values;
    if (container->trailerPresent)
    {
        const std::uint64_t count = container->frameCount;
        auto block = AccountedBlock::allocate(engine, count * 8u, alignof(std::uint64_t));
        if (!block.hasValue())
            return block.status();
        ticks = std::move(*block);
        std::uninitialized_value_construct_n(ticks.values(), static_cast<std::size_t>(count));
        const std::uint64_t trailerOffset = kSerHeaderBytes + count * container->layout.totalBytes();
        for (std::uint64_t done = 0u; done < count;)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            const auto entries =
                static_cast<std::size_t>((std::min)(count - done, static_cast<std::uint64_t>(kTicksPerTransfer)));
            const std::size_t bytes = entries * 8u;
            std::byte* const destination = reinterpret_cast<std::byte*>(ticks.values() + done);
            std::size_t received = 0u;
            if (const int error = file.readAt(destination, bytes, trailerOffset + done * 8u, received); error != 0)
                return ioFailure(error, "the SER trailer could not be read", path);
            if (received != bytes)
                return serFailure(
                    ErrorCode::InvalidData, SequenceFailure::TruncatedContainer,
                    "the container ended inside its trailer", {}, path,
                    {StatusContextField::ofCount(ContextKey::Requested, trailerOffset + count * 8u),
                     StatusContextField::ofCount(ContextKey::Available, trailerOffset + done * 8u + received)});
            const std::span<const std::byte> raw(destination, bytes);
            for (std::size_t entry = 0u; entry < entries; ++entry)
                ticks.values()[done + entry] = readLe64(raw, entry * 8u);
            execution.reportTrace(TraceEventKind::Counter, "ser", "trailer-bytes", static_cast<double>(bytes), "bytes");
            done += entries;
        }
        values = std::span<const std::uint64_t>(ticks.values(), static_cast<std::size_t>(count));
    }
    const TimelineSummary summary = summarize(values);
    const bool timeline = options.timelinePolicy != SerTimelinePolicy::Omit && summary.ordered;
    if (options.timelinePolicy == SerTimelinePolicy::Require && !timeline)
        return serFailure(ErrorCode::InvalidData, SequenceFailure::InvalidTimestamps,
                          "a timeline was required and the trailer does not hold one", "timelinePolicy", path);
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return OpenedContainer{std::move(file), std::move(*container), std::move(ticks), summary, timeline};
}

Result<CodecMetadata> sourceCodecMetadata(const SerContainer& container)
{
    const SerHeader& header = container.header;
    const std::array<std::pair<const char*, std::string>, 7u> fields{
        {{"FileID", header.fileId},
         {"LuID", std::to_string(header.luId)},
         {"ColorID", std::to_string(header.colorId)},
         {"LittleEndian", std::to_string(header.littleEndian)},
         {"PixelDepthPerPlane", std::to_string(header.pixelDepthPerPlane)},
         {"DateTime", std::to_string(header.dateTimeTicks)},
         {"DateTime_UTC", std::to_string(header.dateTimeUtcTicks)}}};
    std::vector<CodecCard> cards;
    cards.reserve(fields.size());
    for (const auto& [keyword, value] : fields)
    {
        auto card = CodecCard::create(CodecNamespace::Ser, keyword, value, {});
        if (!card.hasValue())
            return card.status();
        cards.push_back(std::move(*card));
    }
    return CodecMetadata::create(
        SourceEncoding{.sampleFormat = container.frame.sampleFormat(), .byteOrder = container.frame.byteOrder()},
        std::move(cards));
}
} // namespace an::ser_detail

namespace an
{
Result<SerContainer> inspectSerContainer(std::span<const std::byte> header, std::uint64_t fileBytes,
                                         const SerSourceOptions& options, const EngineLimits& limits)
{
    return sequence_detail::guarded(
        [&]() -> Result<SerContainer> { return ser_detail::inspect(header, fileBytes, options, limits, {}); });
}

Result<SerFileInfo> inspectSerFile(const EngineContext& engine, const ExecutionContext& execution,
                                   std::string_view path, const SerSourceOptions& options)
{
    return sequence_detail::guarded([&]() -> Result<SerFileInfo> {
        auto opened = ser_detail::open(engine, execution, path, options);
        if (!opened.hasValue())
            return opened.status();
        const ser_detail::TimelineSummary& summary = opened->summary;
        return SerFileInfo{.container = std::move(opened->container),
                           .timestampCount = summary.present,
                           .timestampsOrdered = summary.ordered,
                           .timeline = opened->timeline,
                           .firstTimestamp = summary.first,
                           .lastTimestamp = summary.last,
                           .timestampFrameRate = summary.frameRate};
    });
}
} // namespace an
