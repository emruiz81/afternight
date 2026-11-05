// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the codec facade: selecting an adapter, calling it, and holding what it
// returns to the engine context's limits and the adapter's capability record.
//
// Every adapter callback goes through `callCodec`, which runs it as a single-item `parallelFor` so
// the call executes inside the execution's admitted task arena and any parallel work the adapter
// starts inherits the operation's thread grant. A failure the adapter returns is wrapped as
// `CodecFailure::CallbackFailure` with the same code and the adapter's status as its cause. A decode
// reads the header first and checks it before any pixel storage is allocated; an adapter that can
// keep one open source across that check and the decode implements `PreparingImageCodec`, and the
// others are probed and then decoded separately. A read selects its adapter from the registry it is given -
// the engine context's, or one the caller names for the call - and from no other, so everything after the
// selection is the same code whichever registry was named.

#include <AfterNightEngine/IO/CodecIO.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "core/Utf8.h"
#include "io/registry/CodecRegistryAccess.h"
#include "io/registry/PreparedCodecRead.h"
#include "io/registry/PreparedCodecWrite.h"
#include "io/registry/ReservedCodecWrite.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace an
{
namespace
{
struct SelectedCodec
{
    const ImageCodec* codec;
    CodecCapabilities capabilities;
};

bool validPath(std::string_view path) noexcept
{
    return isBoundaryPath(path);
}

Result<SelectedCodec> select(const EngineContext& engine, const ExecutionContext& execution,
                             const CodecRegistry* registry, std::string_view path, CodecFormat format,
                             const char* operation)
{
    if (!engine.valid() || !execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (engine.id() != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (!validPath(path))
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "a codec path must be nonempty bounded UTF-8 without NUL",
                                .operation = operation,
                                .domain = ErrorDomain::Io,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "path")}});
    if (!isCodecFormat(format))
        return codecFailure(ErrorCode::InvalidArgument, CodecFailure::UnknownFormat, format, operation, path);
    if (format == CodecFormat::Unknown)
        format = codecFormatFromPath(path);
    if (format == CodecFormat::Unknown)
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnknownFormat, format, operation, path);
    const auto capabilities = registry != nullptr ? registry->capabilities(format) : std::nullopt;
    if (!capabilities)
        return codecFailure(ErrorCode::Unsupported, CodecFailure::Unavailable, format, operation, path);
    return SelectedCodec{CodecRegistryAccess::find(*registry, format), *capabilities};
}

// The outer boundary also covers allocation of failure detail and validation results. The inner
// one below identifies the actual codec callback and retains its typed causal failure.
template <typename CallableT> auto guarded(const char* operation, CallableT&& body) -> std::invoke_result_t<CallableT>
{
    auto result = evaluateAcrossBoundary({"CodecRegistry", operation, ErrorDomain::Io}, std::forward<CallableT>(body));
    if (!result.hasValue())
        return result.status();
    return std::move(*result);
}

template <typename CallableT>
auto callCodec(const ExecutionContext& execution, CodecFormat format, std::string_view path, const char* operation,
               CallableT&& body) -> std::invoke_result_t<CallableT>
{
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    std::optional<std::invoke_result_t<CallableT>> result;
    const auto scheduled = parallelFor(execution, 1u, {0u, 1u}, [&](TaskRange) -> Status {
        auto called = evaluateAcrossBoundary({codecFormatName(format), operation, ErrorDomain::Io},
                                             std::forward<CallableT>(body));
        if (!called.hasValue())
            return called.status();
        result.emplace(std::move(*called));
        return result->status();
    });
    if (scheduled.failed())
        return codecFailure(scheduled.code(), CodecFailure::CallbackFailure, format, operation, path, scheduled);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    return std::move(*result);
}

Status checkDescription(const EngineContext& engine, const CodecCapabilities& capabilities,
                        const RawFrameDescriptor& descriptor, const ImageLayout& layout, std::uint32_t samples,
                        std::uint8_t layouts)
{
    const auto format = capabilities.format;
    if (layout.extent() != descriptor.extent() || layout.elementSizeBytes() != descriptor.sampleFormat().sizeBytes)
        return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, format, "codec description");
    const ImageLayoutRequest request{descriptor.extent(), descriptor.sampleFormat().sizeBytes, layout.samplesPerPixel(),
                                     layout.planeCount(), 1u};
    const auto& extent = descriptor.extent();
    const auto limits = engine.limits();
    struct Dimension
    {
        const char* name;
        std::uint64_t requested;
        std::uint64_t limit;
    };
    for (const auto dimension : {Dimension{"width", extent.width(), limits.maxImageWidth},
                                 Dimension{"height", extent.height(), limits.maxImageHeight},
                                 Dimension{"channels", extent.channelCount(), limits.maxChannelCount},
                                 Dimension{"bytes", layout.totalBytes(), limits.maxImageBytes}})
        if (dimension.requested > dimension.limit)
            return Status::failure(ErrorCode::LimitExceeded,
                                   {.message = "codec geometry exceeds this context's limits",
                                    .operation = "codec description",
                                    .domain = ErrorDomain::Io,
                                    .context = {StatusContextField::ofText(ContextKey::Parameter, dimension.name),
                                                StatusContextField::ofCount(ContextKey::Requested, dimension.requested),
                                                StatusContextField::ofCount(ContextKey::Limit, dimension.limit)}});
    if (ImageLayout::checkWithStrides(request, layout.rowStrideBytes(), layout.planeStrideBytes(), engine.limits()) !=
        GeometryError::None)
        return Status::failure(
            ErrorCode::LimitExceeded,
            {.message = "codec layout exceeds this context's limits",
             .operation = "codec description",
             .domain = ErrorDomain::Io,
             .context = {StatusContextField::ofCount(ContextKey::Requested, layout.totalBytes()),
                         StatusContextField::ofCount(ContextKey::Limit, engine.limits().maxImageBytes)}});
    if (descriptor.extent().channelCount() > capabilities.maxChannels ||
        (samples & codecSampleBit(descriptor.sampleFormat())) == 0u ||
        (layouts & codecLayoutBit(storageArrangementOf(layout))) == 0u)
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, format,
                            "codec description");
    return Status::success();
}

Status checkSourceMetadata(const ImageMetadata& metadata, CodecFormat format)
{
    return metadata.outputIntent().has_value()
               ? codecFailure(ErrorCode::InvalidData, CodecFailure::InvalidMetadata, format, "codec source metadata")
               : Status::success();
}

Status checkProbe(const EngineContext& engine, const CodecCapabilities& caps, const CodecReadRequest& request,
                  const CodecImageInfo& info)
{
    if ((info.disposition != CodecProbeDisposition::DecodeRequired &&
         info.disposition != CodecProbeDisposition::PayloadValidatedWithoutDecode) ||
        (!request.validatePayload && info.disposition != CodecProbeDisposition::DecodeRequired))
        return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, request.format,
                            "ImageCodec::probe", request.path);
    if (info.imageCount == 0u)
        return Status::failure(ErrorCode::InvalidData);
    if (request.imageIndex >= info.imageCount)
        return Status::failure(ErrorCode::OutOfRange);
    // A probe-only adapter can describe all raw representations without advertising a decoder.
    const auto samples = caps.decodeSamples != 0u ? caps.decodeSamples : (1u << 20u) - 1u;
    if (const auto status =
            checkDescription(engine, caps, info.descriptor, info.decodedLayout, samples, caps.decodeLayouts);
        status.failed())
        return status;
    if (const auto status = checkSourceMetadata(info.metadata, request.format); status.failed())
        return status;
    return {};
}

Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                             const SelectedCodec& selected, CodecReadRequest request)
{
    request.format = selected.capabilities.format;
    auto result = callCodec(execution, request.format, request.path, "ImageCodec::probe",
                            [&] { return selected.codec->probe(engine, execution, request); });
    if (!result.hasValue())
        return result.status();
    if (const auto status = checkProbe(engine, selected.capabilities, request, *result); status.failed())
        return status;
    return std::move(*result);
}

Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecRegistry* registry, CodecReadRequest request, const CodecRegion* region)
{
    const auto selected = select(engine, execution, registry, request.path, request.format, "decodeImage");
    if (!selected.hasValue())
        return selected.status();
    const auto& caps = selected->capabilities;
    request.format = caps.format;
    if (caps.decodeSamples == 0u || (region != nullptr && !caps.regionDecode))
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedOperation, caps.format, "decodeImage",
                            request.path);
    // Preflight needs geometry and sample meaning. Decode supplies metadata and validates the
    // payload; projecting cards or checking the encoded payload here would do that work twice.
    auto headerRequest = request;
    headerRequest.readMetadata = false;
    headerRequest.validatePayload = false;
    std::unique_ptr<PreparedCodecRead> prepared;
    std::optional<CodecImageInfo> probed;
    const CodecImageInfo* info = nullptr;
    if (const auto* preparing = dynamic_cast<const PreparingImageCodec*>(selected->codec))
    {
        auto ready = callCodec(execution, caps.format, request.path, "ImageCodec::prepareRead",
                               [&] { return preparing->prepareRead(engine, execution, request); });
        if (!ready.hasValue())
            return ready.status();
        prepared = std::move(*ready);
        if (!prepared)
            return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, caps.format,
                                "ImageCodec::prepareRead", request.path);
        info = &prepared->info();
        if (const auto status = checkProbe(engine, caps, headerRequest, *info); status.failed())
            return status;
    }
    else
    {
        auto result = probe(engine, execution, *selected, headerRequest);
        if (!result.hasValue())
            return result.status();
        probed.emplace(std::move(*result));
        info = &*probed;
    }
    auto expected = info->descriptor;
    const auto expectedLayout = info->decodedLayout;
    if (region != nullptr)
    {
        const auto& extent = expected.extent();
        if (region->width == 0u || region->height == 0u || region->width > extent.width() ||
            region->height > extent.height() || region->x > extent.width() - region->width ||
            region->y > extent.height() - region->height)
            return Status::failure(ErrorCode::OutOfRange);
        auto cropped = expected.toRequest();
        cropped.extent = *ImageExtent::create(region->width, region->height, extent.channelCount(), engine.limits());
        if (cropped.cfa.isMosaiced())
        {
            const auto block = cfaPatternBlockSize(cropped.cfa.pattern());
            const auto x = (cropped.cfa.originX() + region->x % block) % block;
            const auto y = (cropped.cfa.originY() + region->y % block) % block;
            cropped.cfa = cropped.cfa.hasFilterTable()
                              ? *CfaDescriptor::create(cropped.cfa.filterTable().entries(), x, y)
                              : *CfaDescriptor::create(cropped.cfa.pattern(), x, y);
        }
        expected = *RawFrameDescriptor::create(cropped);
    }
    auto result = callCodec(execution, caps.format, request.path,
                            region != nullptr ? "ImageCodec::decodeRegion" : "ImageCodec::decode", [&] {
                                if (prepared)
                                    return prepared->decode(engine, execution, request, region);
                                return region != nullptr
                                           ? selected->codec->decodeRegion(engine, execution, request, *region)
                                           : selected->codec->decode(engine, execution, request);
                            });
    if (!result.hasValue())
        return result.status();
    if (!result->frame.valid() || result->frame.descriptor() != expected ||
        (region == nullptr && result->frame.layout() != expectedLayout))
        return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, caps.format, "decodeImage",
                            request.path);
    if (const auto status =
            checkDescription(engine, caps, expected, *result->frame.layout(), caps.decodeSamples, caps.decodeLayouts);
        status.failed())
        return status;
    if (result->frame.buffer().ownership() == SampleBufferOwnership::Borrowed)
        return codecFailure(ErrorCode::InvalidData, CodecFailure::UnsafeLifetime, caps.format, "decodeImage",
                            request.path);
    if (result->frame.buffer().ownership() == SampleBufferOwnership::Owned &&
        result->frame.buffer().contextId() != engine.id())
        return codecFailure(ErrorCode::IncompatibleArguments, CodecFailure::ForeignAllocation, caps.format,
                            "decodeImage", request.path);
    if (const auto status = checkSourceMetadata(result->metadata, caps.format); status.failed())
        return status;
    return std::move(*result);
}

Result<CodecImageInfo> probeThrough(const EngineContext& engine, const ExecutionContext& execution,
                                    const CodecRegistry* registry, const CodecReadRequest& request)
{
    return guarded("probeImage", [&]() -> Result<CodecImageInfo> {
        const auto selected = select(engine, execution, registry, request.path, request.format, "probeImage");
        if (!selected.hasValue())
            return selected.status();
        return probe(engine, execution, *selected, request);
    });
}
} // namespace

Result<CodecImageInfo> probeImage(const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request)
{
    return probeThrough(engine, execution, engine.codecRegistry(), request);
}

Result<CodecDecodeResult> decodeImage(const EngineContext& engine, const ExecutionContext& execution,
                                      const CodecReadRequest& request)
{
    return guarded("decodeImage", [&] { return decode(engine, execution, engine.codecRegistry(), request, nullptr); });
}

Result<CodecDecodeResult> decodeImageRegion(const EngineContext& engine, const ExecutionContext& execution,
                                            const CodecReadRequest& request, const CodecRegion& region)
{
    return guarded("decodeImageRegion",
                   [&] { return decode(engine, execution, engine.codecRegistry(), request, &region); });
}

Result<CodecImageInfo> probeImage(const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecRegistry& registry, const CodecReadRequest& request)
{
    return probeThrough(engine, execution, &registry, request);
}

Result<CodecDecodeResult> decodeImage(const EngineContext& engine, const ExecutionContext& execution,
                                      const CodecRegistry& registry, const CodecReadRequest& request)
{
    return guarded("decodeImage", [&] { return decode(engine, execution, &registry, request, nullptr); });
}

Result<CodecDecodeResult> decodeImageRegion(const EngineContext& engine, const ExecutionContext& execution,
                                            const CodecRegistry& registry, const CodecReadRequest& request,
                                            const CodecRegion& region)
{
    return guarded("decodeImageRegion", [&] { return decode(engine, execution, &registry, request, &region); });
}

/// What an open row reader keeps: its engine, the registry it selected its adapter from, its own copy of
/// the request, the header and, when the adapter decodes rows into caller memory, the preparation it
/// decodes them through.
struct ImageRowReader::State
{
    const EngineContext* engine = nullptr;
    const CodecRegistry* registry = nullptr;
    std::string path;
    CodecReadRequest request{};
    std::optional<CodecImageInfo> info;
    std::unique_ptr<PreparedCodecRead> prepared;
};

ImageRowReader::ImageRowReader() noexcept = default;
ImageRowReader::~ImageRowReader() = default;
ImageRowReader::ImageRowReader(ImageRowReader&& other) noexcept = default;
ImageRowReader& ImageRowReader::operator=(ImageRowReader&& other) noexcept = default;

Result<ImageRowReader> ImageRowReader::open(const EngineContext& engine, const ExecutionContext& execution,
                                            const CodecReadRequest& request)
{
    return guarded("ImageRowReader::open",
                   [&]() -> Result<ImageRowReader> { return openThrough(engine, execution, nullptr, request); });
}

Result<ImageRowReader> ImageRowReader::open(const EngineContext& engine, const ExecutionContext& execution,
                                            const CodecRegistry& registry, const CodecReadRequest& request)
{
    return guarded("ImageRowReader::open",
                   [&]() -> Result<ImageRowReader> { return openThrough(engine, execution, &registry, request); });
}

Result<ImageRowReader> ImageRowReader::openThrough(const EngineContext& engine, const ExecutionContext& execution,
                                                   const CodecRegistry* named, const CodecReadRequest& request)
{
    const CodecRegistry* registry = named != nullptr ? named : engine.codecRegistry();
    const auto selected = select(engine, execution, registry, request.path, request.format, "ImageRowReader::open");
    if (!selected.hasValue())
        return selected.status();
    const auto& caps = selected->capabilities;
    if (caps.decodeSamples == 0u || !caps.regionDecode)
        return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedOperation, caps.format,
                            "ImageRowReader::open", request.path);
    auto state = std::make_unique<State>();
    state->engine = &engine;
    state->registry = registry;
    state->path = std::string(request.path);
    state->request = request;
    state->request.path = state->path;
    state->request.format = caps.format;
    state->request.readMetadata = false;
    state->request.validatePayload = false;
    if (const auto* preparing = dynamic_cast<const PreparingImageCodec*>(selected->codec))
    {
        auto ready = callCodec(execution, caps.format, state->path, "ImageCodec::prepareRead",
                               [&] { return preparing->prepareRead(engine, execution, state->request); });
        if (!ready.hasValue())
            return ready.status();
        std::unique_ptr<PreparedCodecRead> prepared = std::move(*ready);
        if (!prepared)
            return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, caps.format,
                                "ImageCodec::prepareRead", request.path);
        if (const auto status = checkProbe(engine, caps, state->request, prepared->info()); status.failed())
            return status;
        state->info.emplace(prepared->info());
        if (prepared->decodesRowsInto())
            state->prepared = std::move(prepared);
    }
    else
    {
        auto probed = probe(engine, execution, *selected, state->request);
        if (!probed.hasValue())
            return probed.status();
        state->info.emplace(std::move(*probed));
    }
    ImageRowReader reader;
    reader.m_state = std::move(state);
    return reader;
}

bool ImageRowReader::valid() const noexcept
{
    return m_state != nullptr;
}

const CodecImageInfo* ImageRowReader::info() const noexcept
{
    return m_state != nullptr ? &*m_state->info : nullptr;
}

std::uint32_t ImageRowReader::decodeGranularityRows() const noexcept
{
    if (m_state == nullptr)
        return 0u;
    // An adapter read through region decodes says nothing of how it decodes them.
    if (m_state->prepared == nullptr)
        return 1u;
    const std::uint32_t height = m_state->info->descriptor.extent().height();
    return (std::clamp)(m_state->prepared->decodeGranularityRows(), 1u, (std::max)(height, 1u));
}

Result<ImageLayout> ImageRowReader::rowsLayout(std::uint32_t rows) const
{
    if (m_state == nullptr)
        return Status::failure(ErrorCode::NotInitialized);
    const ImageExtent& extent = m_state->info->descriptor.extent();
    if (rows == 0u || rows > extent.height())
        return Status::failure(ErrorCode::OutOfRange);
    const auto band = ImageExtent::create(extent.width(), rows, extent.channelCount(), m_state->engine->limits());
    const bool interleaved = m_state->info->decodedLayout.samplesPerPixel() > 1u;
    const auto layout = band.has_value()
                            ? ImageLayout::create({.extent = *band,
                                                   .elementSizeBytes = m_state->info->decodedLayout.elementSizeBytes(),
                                                   .samplesPerPixel = interleaved ? extent.channelCount() : 1u,
                                                   .planeCount = interleaved ? 1u : extent.channelCount(),
                                                   .rowAlignmentBytes = 1u},
                                                  m_state->engine->limits())
                            : std::nullopt;
    if (!layout.has_value())
        return Status::failure(ErrorCode::LimitExceeded);
    return *layout;
}

Result<ImageLayout> ImageRowReader::readRows(const ExecutionContext& execution, std::uint32_t firstRow,
                                             std::uint32_t rows, std::span<std::byte> destination)
{
    return guarded("ImageRowReader::readRows", [&]() -> Result<ImageLayout> {
        if (m_state == nullptr || !execution.valid())
            return Status::failure(ErrorCode::NotInitialized);
        if (execution.engineContextId() != m_state->engine->id())
            return Status::failure(ErrorCode::IncompatibleArguments);
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const ImageExtent& extent = m_state->info->descriptor.extent();
        if (rows == 0u || firstRow >= extent.height() || rows > extent.height() - firstRow)
            return Status::failure(ErrorCode::OutOfRange);
        const Result<ImageLayout> layout = rowsLayout(rows);
        if (!layout.hasValue())
            return layout.status();
        if (destination.size() < layout->totalBytes())
            return Status::failure(
                ErrorCode::IncompatibleArguments,
                {.message = "destination is shorter than the band",
                 .operation = "ImageRowReader::readRows",
                 .domain = ErrorDomain::Io,
                 .context = {StatusContextField::ofText(ContextKey::Parameter, "destination"),
                             StatusContextField::ofCount(ContextKey::Requested, layout->totalBytes()),
                             StatusContextField::ofCount(ContextKey::Limit, destination.size())}});
        const CodecFormat format = m_state->request.format;
        if (m_state->prepared != nullptr)
        {
            auto read = callCodec(
                execution, format, m_state->path, "PreparedCodecRead::decodeRowsInto", [&]() -> Result<ImageLayout> {
                    const Status decoded = m_state->prepared->decodeRowsInto(
                        *m_state->engine, execution, m_state->request, firstRow, rows, destination, *layout);
                    if (decoded.failed())
                        return decoded;
                    return *layout;
                });
            if (!read.hasValue())
                return read.status();
            return *layout;
        }
        // Read through the facade and copied into the tight band, whatever padding the adapter kept.
        const CodecRegion region{0u, firstRow, extent.width(), rows};
        auto decoded = decode(*m_state->engine, execution, m_state->registry, m_state->request, &region);
        if (!decoded.hasValue())
            return decoded.status();
        const auto source = decoded->frame.layout();
        if (!source.has_value() || source->samplesPerPixel() != layout->samplesPerPixel() ||
            source->planeCount() != layout->planeCount() || source->elementSizeBytes() != layout->elementSizeBytes())
            return codecFailure(ErrorCode::InvalidData, CodecFailure::DescriptorMismatch, format,
                                "ImageRowReader::readRows", m_state->path);
        const std::uint64_t rowBytes = layout->minimumRowBytes();
        for (std::uint32_t plane = 0u; plane < layout->planeCount(); ++plane)
            for (std::uint32_t y = 0u; y < rows; ++y)
                std::memcpy(destination.data() + plane * layout->planeStrideBytes() + y * layout->rowStrideBytes(),
                            decoded->frame.bytes().data() + plane * source->planeStrideBytes() +
                                y * source->rowStrideBytes(),
                            static_cast<std::size_t>(rowBytes));
        return *layout;
    });
}

Result<CodecEncodeReport> encodeImage(const EngineContext& engine, const ExecutionContext& execution,
                                      const CodecEncodeRequest& request, const RawFrame& frame,
                                      const ImageMetadata& metadata)
{
    return encodePreparedImage(engine, execution, request, frame, metadata, nullptr);
}

bool supportsPreparedCodecWrite(const EngineContext& engine, CodecFormat format) noexcept
{
    const auto* registry = engine.codecRegistry();
    return registry != nullptr &&
           dynamic_cast<const StreamingImageCodec*>(CodecRegistryAccess::find(*registry, format)) != nullptr;
}

Result<CodecEncodeReport> encodePreparedImage(const EngineContext& engine, const ExecutionContext& execution,
                                              const CodecEncodeRequest& request, const RawFrame& frame,
                                              const ImageMetadata& metadata, CodecOutput* output)
{
    return guarded("encodeImage", [&]() -> Result<CodecEncodeReport> {
        const auto selected =
            select(engine, execution, engine.codecRegistry(), request.pendingPath, request.format, "encodeImage");
        if (!selected.hasValue())
            return selected.status();
        const auto& caps = selected->capabilities;
        if (caps.encodeSamples == 0u)
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedOperation, caps.format, "encodeImage",
                                request.pendingPath);
        if (!frame.valid())
            return Status::failure(ErrorCode::InvalidArgument);
        if (const auto status = checkDescription(engine, caps, *frame.descriptor(), *frame.layout(), caps.encodeSamples,
                                                 caps.encodeLayouts);
            status.failed())
            return status;
        if (request.intent != nullptr)
        {
            const auto descriptor = *frame.descriptor();
            const auto order = [](ByteOrder value) { return value == ByteOrder::Native ? nativeByteOrder() : value; };
            if (descriptor.residentSampleType() != request.intent->sampleType() ||
                order(descriptor.byteOrder()) != order(request.intent->byteOrder()) ||
                request.intent->scaling() == OutputScaling::NormalizeFullRange)
                return codecFailure(ErrorCode::IncompatibleArguments, CodecFailure::UnsupportedRepresentation,
                                    caps.format, "encodeImage", request.pendingPath);
        }
        auto resolved = request;
        resolved.format = caps.format;
        return callCodec(execution, caps.format, request.pendingPath, "ImageCodec::encode", [&] {
            if (output != nullptr)
            {
                const auto* streaming = dynamic_cast<const StreamingImageCodec*>(selected->codec);
                if (streaming == nullptr)
                    return Result<CodecEncodeReport>(Status::failure(ErrorCode::Unsupported));
                return streaming->encodeTo(engine, execution, resolved, frame, metadata, *output);
            }
            return selected->codec->encode(engine, execution, resolved, frame, metadata);
        });
    });
}

bool supportsReservedCodecWrite(const EngineContext& engine, CodecFormat format) noexcept
{
    const auto* registry = engine.codecRegistry();
    return registry != nullptr && isCodecFormat(format) && format != CodecFormat::Unknown &&
           dynamic_cast<const ReservingImageCodec*>(CodecRegistryAccess::find(*registry, format)) != nullptr;
}

Result<std::unique_ptr<CodecReservedRows>>
reservePreparedImage(const EngineContext& engine, const ExecutionContext& execution, const CodecEncodeRequest& request,
                     const RawFrameDescriptor& descriptor, StorageArrangement arrangement,
                     const ImageMetadata& metadata)
{
    using Reserved = Result<std::unique_ptr<CodecReservedRows>>;
    return guarded("reserveImage", [&]() -> Reserved {
        const auto selected =
            select(engine, execution, engine.codecRegistry(), request.pendingPath, request.format, "reserveImage");
        if (!selected.hasValue())
            return selected.status();
        const auto& caps = selected->capabilities;
        const auto* reserving = dynamic_cast<const ReservingImageCodec*>(selected->codec);
        if (caps.encodeSamples == 0u || reserving == nullptr)
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedOperation, caps.format, "reserveImage",
                                request.pendingPath);
        // The room is described as the tight layout of the arrangement stated, which is what the capability record
        // and the limits judge a frame's description by.
        const bool interleaved = arrangement == StorageArrangement::Interleaved;
        const auto& extent = descriptor.extent();
        if (!interleaved && arrangement != StorageArrangement::Planar)
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, caps.format,
                                "reserveImage", request.pendingPath);
        const auto layout = ImageLayout::create({.extent = extent,
                                                 .elementSizeBytes = descriptor.sampleFormat().sizeBytes,
                                                 .samplesPerPixel = interleaved ? extent.channelCount() : 1u,
                                                 .planeCount = interleaved ? 1u : extent.channelCount(),
                                                 .rowAlignmentBytes = 1u},
                                                engine.limits());
        if (!layout.has_value())
            return Status::failure(ErrorCode::LimitExceeded,
                                   {.message = "the reserved image has no layout under this context's limits",
                                    .operation = "reserveImage",
                                    .domain = ErrorDomain::Io});
        if (const auto status =
                checkDescription(engine, caps, descriptor, *layout, caps.encodeSamples, caps.encodeLayouts);
            status.failed())
            return status;
        const auto order = [](ByteOrder value) { return value == ByteOrder::Native ? nativeByteOrder() : value; };
        if (order(descriptor.byteOrder()) != nativeByteOrder())
            return codecFailure(ErrorCode::Unsupported, CodecFailure::UnsupportedRepresentation, caps.format,
                                "reserveImage", request.pendingPath);
        if (request.intent != nullptr && (descriptor.residentSampleType() != request.intent->sampleType() ||
                                          order(descriptor.byteOrder()) != order(request.intent->byteOrder()) ||
                                          request.intent->scaling() == OutputScaling::NormalizeFullRange))
            return codecFailure(ErrorCode::IncompatibleArguments, CodecFailure::UnsupportedRepresentation, caps.format,
                                "reserveImage", request.pendingPath);
        auto resolved = request;
        resolved.format = caps.format;
        return callCodec(execution, caps.format, request.pendingPath, "ReservingImageCodec::reserve", [&] {
            return reserving->reserve(engine, execution, resolved, descriptor, storageArrangementOf(*layout), metadata);
        });
    });
}

Status writeReservedRows(const ExecutionContext& execution, CodecFormat format, std::string_view path,
                         CodecReservedRows& rows, std::uint32_t firstRow, const ImageLayout& layout,
                         std::span<const std::byte> samples)
{
    // A row write is I/O alone and may run on several threads at once, so it takes the exception boundary without the
    // single-item schedule the other callbacks run under.
    Status written;
    const Status crossed =
        callAcrossBoundary({codecFormatName(format), "CodecReservedRows::writeRows", ErrorDomain::Io},
                           [&] { written = rows.writeRows(execution, firstRow, layout, samples); });
    if (crossed.failed())
        return codecFailure(crossed.code(), CodecFailure::CallbackFailure, format, "CodecReservedRows::writeRows", path,
                            crossed);
    if (written.failed())
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return codecFailure(written.code(), CodecFailure::CallbackFailure, format, "CodecReservedRows::writeRows", path,
                            written);
    }
    return Status::success();
}

Result<std::uint64_t> finishReservedRows(const ExecutionContext& execution, CodecFormat format, std::string_view path,
                                         CodecReservedRows& rows)
{
    return callCodec(execution, format, path, "CodecReservedRows::finish", [&] { return rows.finish(execution); });
}
} // namespace an
