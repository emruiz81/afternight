// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the XISF adapter: its capabilities, its prepared read, the factories, and the
// library-memory and bounded-region queries.
//
// A prepared read opens the unit and describes the selected image once, and decodes through the
// same reader after the facade has checked that description, so the XML header is parsed once per
// decode - or once per row reader, which decodes band after band through it. A read that asks for no
// metadata opens the header alone, the cheapest open there is.

#include <AfterNightEngine/IO/XisfCodec.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "core/Utf8.h"
#include "io/registry/PreparedCodecRead.h"
#include "io/xisf/XisfFile.h"

#include <utility>

namespace an
{
namespace
{
/// One open unit, described once and decoded through the same reader.
class XisfPreparedRead final : public PreparedCodecRead
{
public:
    XisfPreparedRead(XisfHandle source, XisfHeader header, XisfCodecOptions options)
        : m_source(std::move(source)), m_header(std::move(header)), m_options(options)
    {
    }
    const CodecImageInfo& info() const noexcept override
    {
        return m_header.info;
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request, const CodecRegion* region) override
    {
        return readPreparedXisf(engine, execution, request, region, m_options, m_source, std::move(m_header));
    }
    bool decodesRowsInto() const noexcept override
    {
        return true;
    }
    std::uint32_t decodeGranularityRows() const noexcept override
    {
        return xisfDecodeGranularityRows(m_header, m_options);
    }
    Status decodeRowsInto(const EngineContext&, const ExecutionContext& execution, const CodecReadRequest& request,
                          std::uint32_t firstRow, std::uint32_t rows, std::span<std::byte> destination,
                          const ImageLayout& layout) override
    {
        const CodecRegion area{0u, firstRow, m_header.info.descriptor.extent().width(), rows};
        return readXisfPixels(execution, request, area, m_header, *m_source.reader, m_options, destination.data(),
                              layout);
    }

private:
    XisfHandle m_source;
    XisfHeader m_header;
    XisfCodecOptions m_options;
};

class XisfCodec final : public PreparingImageCodec, public ReservingImageCodec
{
public:
    explicit XisfCodec(const XisfCodecOptions& options) : m_options(options) {}
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto source = openXisf(request.path, engine.limits(), !request.readMetadata);
        if (!source.hasValue())
            return source.status();
        auto header = readXisfHeader(*source->reader, engine, execution, request);
        if (!header.hasValue())
            return header.status();
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<XisfPreparedRead>(std::move(*source), std::move(*header), m_options)};
    }
    CodecCapabilities capabilities() const override
    {
        // The six representations XISF names that this engine can describe. Eight bytes of unsigned
        // integer is not a resident representation and is carried as exact codes; the two complex
        // representations have no meaning in this engine's image model and are refused by name.
        std::uint32_t samples = codecSampleBit({1u, RawSampleClass::UnsignedInteger}) |
                                codecSampleBit({2u, RawSampleClass::UnsignedInteger}) |
                                codecSampleBit({4u, RawSampleClass::UnsignedInteger}) |
                                codecSampleBit({8u, RawSampleClass::UnsignedInteger}) |
                                codecSampleBit({4u, RawSampleClass::FloatingPoint}) |
                                codecSampleBit({8u, RawSampleClass::FloatingPoint});
        const auto layouts = static_cast<std::uint8_t>(codecLayoutBit(StorageArrangement::Planar) |
                                                       codecLayoutBit(StorageArrangement::Interleaved));
        return {CodecFormat::Xisf, samples, samples, layouts, layouts, 65535u, true};
    }
    Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecReadRequest& request) const override
    {
        auto source = openXisf(request.path, engine.limits(), !request.readMetadata);
        if (!source.hasValue())
            return source.status();
        auto header = readXisfHeader(*source->reader, engine, execution, request);
        if (!header.hasValue())
            return header.status();
        return std::move(header->info);
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request) const override
    {
        return readXisf(engine, execution, request, nullptr, m_options);
    }
    Result<CodecDecodeResult> decodeRegion(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion& region) const override
    {
        return readXisf(engine, execution, request, &region, m_options);
    }
    Result<CodecEncodeReport> encode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecEncodeRequest& request, const RawFrame& frame,
                                     const ImageMetadata& metadata) const override
    {
        return writeXisf(engine, execution, request, frame, metadata);
    }
    Result<std::unique_ptr<CodecReservedRows>> reserve(const EngineContext& engine, const ExecutionContext& execution,
                                                       const CodecEncodeRequest& request,
                                                       const RawFrameDescriptor& descriptor,
                                                       StorageArrangement arrangement,
                                                       const ImageMetadata& metadata) const override
    {
        return reserveXisf(engine, execution, request, descriptor, arrangement, metadata);
    }

private:
    XisfCodecOptions m_options;
};

/// The checked header of one image, read without metadata for a query about how its pixels are read.
///
/// The header alone is read: the block's facts are in it, and loading the unit's ancillary data would itself allocate
/// what these queries are about.
Result<XisfHeader> queriedHeader(const EngineContext& engine, const ExecutionContext& execution, std::string_view path,
                                 std::uint32_t imageIndex, XisfHandle& source)
{
    if (!engine.valid() || !execution.valid())
        return Status::failure(ErrorCode::NotInitialized);
    if (engine.id() != execution.engineContextId())
        return Status::failure(ErrorCode::IncompatibleArguments);
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    if (!isBoundaryPath(path))
        return Status::failure(ErrorCode::InvalidArgument);
    auto opened = openXisf(path, engine.limits(), true);
    if (!opened.hasValue())
        return opened.status();
    source = std::move(*opened);
    auto header =
        readXisfHeader(*source.reader, engine, execution,
                       {.path = path, .imageIndex = imageIndex, .readMetadata = false, .validatePayload = false});
    if (!header.hasValue())
        return header.status();
    if (header->blockFailure.failed())
        return header->blockFailure;
    if (!header->block)
        return xisfFailure(ErrorCode::Internal, CodecFailure::PayloadUnreadable, "describe XISF pixel data", path,
                           "the library described an image without its block");
    return std::move(*header);
}

/// What a read of an area of an image takes from the library, behind the boundary every query keeps.
Result<std::uint64_t> libraryBytes(const EngineContext& engine, const ExecutionContext& execution,
                                   std::string_view path, const CodecRegion* region, std::uint32_t imageIndex,
                                   const XisfCodecOptions& options)
{
    const auto answered = evaluateAcrossBoundary(
        {"OpenXISF", "bound XISF library memory", ErrorDomain::Io}, [&]() -> Result<std::uint64_t> {
            XisfHandle source;
            auto header = queriedHeader(engine, execution, path, imageIndex, source);
            if (!header.hasValue())
                return header.status();
            const auto& extent = header->info.descriptor.extent();
            const CodecRegion area = region != nullptr ? *region : CodecRegion{0u, 0u, extent.width(), extent.height()};
            if (area.width == 0u || area.height == 0u || area.width > extent.width() || area.height > extent.height() ||
                area.x > extent.width() - area.width || area.y > extent.height() - area.height)
                return Status::failure(ErrorCode::OutOfRange);
            return xisfReadLibraryBytes(execution, *header, area, options);
        });
    return answered.hasValue() ? *answered : Result<std::uint64_t>(answered.status());
}
} // namespace

Result<std::shared_ptr<const ImageCodec>> createXisfCodec()
{
    return createXisfCodec(XisfCodecOptions{});
}

Result<std::shared_ptr<const ImageCodec>> createXisfCodec(const XisfCodecOptions& options)
{
    return evaluateAcrossBoundary({"OpenXISF", "createXisfCodec", ErrorDomain::Io}, [&] {
        return std::shared_ptr<const ImageCodec>{std::make_shared<XisfCodec>(options)};
    });
}

Result<std::uint64_t> xisfDecodeLibraryBytes(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path, std::uint32_t imageIndex)
{
    return libraryBytes(engine, execution, path, nullptr, imageIndex, XisfCodecOptions{});
}

Result<std::uint64_t> xisfDecodeLibraryBytes(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path, std::uint32_t imageIndex,
                                             const XisfCodecOptions& options)
{
    return libraryBytes(engine, execution, path, nullptr, imageIndex, options);
}

Result<std::uint64_t> xisfRegionDecodeLibraryBytes(const EngineContext& engine, const ExecutionContext& execution,
                                                   std::string_view path, const CodecRegion& region,
                                                   std::uint32_t imageIndex, const XisfCodecOptions& options)
{
    return libraryBytes(engine, execution, path, &region, imageIndex, options);
}

Result<bool> supportsBoundedXisfRegionDecode(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path, const XisfCodecOptions& options)
{
    const auto checked =
        evaluateAcrossBoundary({"OpenXISF", "probe XISF region support", ErrorDomain::Io}, [&]() -> Result<bool> {
            XisfHandle source;
            auto header = queriedHeader(engine, execution, path, 0u, source);
            if (!header.hasValue())
                return header.status();
            return xisfBlockReadInParts(*header->block, options);
        });
    return checked.hasValue() ? *checked : Result<bool>(checked.status());
}
} // namespace an
