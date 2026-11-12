// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FITS adapter: its capabilities, its prepared read, and the factory.
//
// A prepared read opens the file once, reads the selected image's header without counting every image
// or validating the payload, and decodes from the same reader once the facade has checked that
// description; a row reader keeps it open and reads band after band from it. The capabilities decode every integer
// width except unsigned 64-bit - and signed 64-bit only when the policy preserves raw codes - and encode all ten FITS
// representations, planar only.

#include <AfterNightEngine/IO/FitsCodec.h>

#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/fits/FitsFile.h"
#include "io/registry/PreparedCodecRead.h"
#include "core/Utf8.h"

namespace an
{
namespace
{
class FitsPreparedRead final : public PreparedCodecRead
{
public:
    FitsPreparedRead(FitsHandle file, FitsHeader header, FitsInteger64Policy integer64)
        : m_file(std::move(file)), m_header(std::move(header)), m_integer64(integer64)
    {
    }
    const CodecImageInfo& info() const noexcept override
    {
        return m_header.info;
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request, const CodecRegion* region) override
    {
        return readPreparedFits(engine, execution, request, region, m_integer64, *m_file.reader, std::move(m_header),
                                m_file.tiles.get());
    }
    bool decodesRowsInto() const noexcept override
    {
        return true;
    }
    Status decodeRowsInto(const EngineContext& engine, const ExecutionContext& execution,
                          const CodecReadRequest& request, std::uint32_t firstRow, std::uint32_t rows,
                          std::span<std::byte> destination, const ImageLayout& layout) override
    {
        const CodecRegion area{0u, firstRow, m_header.info.descriptor.extent().width(), rows};
        return readFitsPixels(engine, execution, request, area, m_integer64, *m_file.reader, m_header,
                              destination.data(), layout, m_file.tiles.get());
    }

private:
    FitsHandle m_file;
    FitsHeader m_header;
    FitsInteger64Policy m_integer64;
};

class FitsCodec final : public PreparingImageCodec
{
public:
    explicit FitsCodec(FitsInteger64Policy policy) : m_integer64(policy) {}
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto file = openFits(request.path, engine.limits());
        if (!file.hasValue())
            return file.status();
        auto headerRequest = request;
        headerRequest.validatePayload = false;
        auto header = readFitsHeader(*file, engine, execution, headerRequest, m_integer64, false);
        if (!header.hasValue())
            return header.status();
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<FitsPreparedRead>(std::move(*file), std::move(*header), m_integer64)};
    }
    CodecCapabilities capabilities() const override
    {
        std::uint32_t samples = 0u;
        for (const auto kind : {RawSampleClass::SignedInteger, RawSampleClass::UnsignedInteger})
            for (const auto width : {1u, 2u, 4u, 8u})
                samples |= codecSampleBit({static_cast<std::uint8_t>(width), kind});
        samples |=
            codecSampleBit({4u, RawSampleClass::FloatingPoint}) | codecSampleBit({8u, RawSampleClass::FloatingPoint});
        const auto planar = codecLayoutBit(StorageArrangement::Planar);
        auto decoded = samples & ~codecSampleBit({8u, RawSampleClass::UnsignedInteger});
        if (m_integer64 != FitsInteger64Policy::PreserveRaw)
            decoded &= ~codecSampleBit({8u, RawSampleClass::SignedInteger});
        return {CodecFormat::Fits, decoded, samples, planar, planar, 65535u, true};
    }
    Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecReadRequest& request) const override
    {
        auto file = openFits(request.path, engine.limits());
        if (!file.hasValue())
            return file.status();
        auto header = readFitsHeader(*file, engine, execution, request, m_integer64);
        if (!header.hasValue())
            return header.status();
        return std::move(header->info);
    }
    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request) const override
    {
        return readFits(engine, execution, request, nullptr, m_integer64);
    }
    Result<CodecDecodeResult> decodeRegion(const EngineContext& engine, const ExecutionContext& execution,
                                           const CodecReadRequest& request, const CodecRegion& region) const override
    {
        return readFits(engine, execution, request, &region, m_integer64);
    }
    Result<CodecEncodeReport> encode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecEncodeRequest& request, const RawFrame& frame,
                                     const ImageMetadata& metadata) const override
    {
        return writeFits(engine, execution, request, frame, metadata);
    }

private:
    FitsInteger64Policy m_integer64;
};
} // namespace

Result<std::shared_ptr<const ImageCodec>> createFitsCodec(FitsInteger64Policy integer64)
{
    if (integer64 > FitsInteger64Policy::PreserveRaw)
        return Status::failure(ErrorCode::InvalidArgument);
    return evaluateAcrossBoundary(
        {"OpenFITS", "createFitsCodec", ErrorDomain::Io},
        [&]() -> std::shared_ptr<const ImageCodec> { return std::make_shared<FitsCodec>(integer64); });
}

Result<bool> supportsBoundedFitsRegionDecode(const EngineContext& engine, const ExecutionContext& execution,
                                             std::string_view path)
{
    const auto checked =
        evaluateAcrossBoundary({"OpenFITS", "probe FITS region support", ErrorDomain::Io}, [&]() -> Result<bool> {
            if (!engine.valid() || !execution.valid())
                return Status::failure(ErrorCode::NotInitialized);
            if (engine.id() != execution.engineContextId())
                return Status::failure(ErrorCode::IncompatibleArguments);
            if (const auto stopped = execution.checkCancellation(); stopped.failed())
                return stopped;
            if (!isBoundaryPath(path))
                return Status::failure(ErrorCode::InvalidArgument);
            auto file = openFits(path, engine.limits());
            if (!file.hasValue())
                return file.status();
            auto header = readFitsHeader(*file, engine, execution,
                                         {.path = path, .readMetadata = false, .validatePayload = false},
                                         FitsInteger64Policy::PreserveRaw);
            if (!header.hasValue())
                return header.status();
            return !header->compressed;
        });
    return checked.hasValue() ? *checked : Result<bool>(checked.status());
}
} // namespace an
