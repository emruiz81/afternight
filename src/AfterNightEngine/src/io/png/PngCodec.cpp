// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the PNG adapter: its capabilities, its prepared read, the factory, and the
// version report for the linked libpng.
//
// A prepared read opens the file and reads its header once, and decodes through the same session
// after the facade has checked that description; a region is refused as unsupported, because this
// adapter has no region reader. The capabilities are eight- and sixteen-bit unsigned samples in one
// to four channels. A decode yields the container's interleaved samples, and an encode takes either
// arrangement, interleaving a planar frame one row at a time.

#include <AfterNightEngine/IO/PngCodec.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/png/PngFile.h"
#include "io/registry/PreparedCodecRead.h"
#include "runtime/DependencyVersions.h"

#include <utility>

namespace an
{
namespace
{

/// One open container, described once and decoded through the same session.
class PngPreparedRead final : public PreparedCodecRead
{
public:
    /// Takes over an open session and the description read from it.
    ///
    /// @param session the open source.
    /// @param info what its header states.
    PngPreparedRead(PngReadHandle session, CodecImageInfo info) : m_session(std::move(session)), m_info(std::move(info))
    {
    }

    const CodecImageInfo& info() const noexcept override
    {
        return m_info;
    }

    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request, const CodecRegion* region) override
    {
        if (region != nullptr)
        {
            return Status::failure(ErrorCode::Unsupported);
        }
        return readPng(engine, execution, request, *m_session, std::move(m_info));
    }

private:
    /// The open source, closed when this preparation is released.
    PngReadHandle m_session;

    /// What its header states.
    CodecImageInfo m_info;
};

/// The adapter itself: immutable, and holding nothing between calls.
class PngCodec final : public PreparingImageCodec
{
public:
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto session = openPng(engine, request.path);
        if (!session.hasValue())
        {
            return session.status();
        }
        auto info = describePng(**session, engine, request);
        if (!info.hasValue())
        {
            return info.status();
        }
        static_cast<void>(execution);
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<PngPreparedRead>(std::move(*session), std::move(*info))};
    }

    CodecCapabilities capabilities() const override
    {
        // Two representations, because a PNG file states two: eight- and sixteen-bit unsigned
        // samples, in one to four channels. The container interleaves them, and a single-channel
        // image satisfies both arrangements, so both are advertised.
        const auto samples = codecSampleBit({1u, RawSampleClass::UnsignedInteger}) |
                             codecSampleBit({2u, RawSampleClass::UnsignedInteger});
        const auto layouts = static_cast<std::uint8_t>(codecLayoutBit(StorageArrangement::Planar) |
                                                       codecLayoutBit(StorageArrangement::Interleaved));
        return {CodecFormat::Png, samples, samples, layouts, layouts, 4u, false};
    }

    Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                 const CodecReadRequest& request) const override
    {
        auto prepared = prepareRead(engine, execution, request);
        if (!prepared.hasValue())
        {
            return prepared.status();
        }
        return (*prepared)->info();
    }

    Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecReadRequest& request) const override
    {
        auto prepared = prepareRead(engine, execution, request);
        if (!prepared.hasValue())
        {
            return prepared.status();
        }
        return (*prepared)->decode(engine, execution, request, nullptr);
    }

    Result<CodecEncodeReport> encode(const EngineContext& engine, const ExecutionContext& execution,
                                     const CodecEncodeRequest& request, const RawFrame& frame,
                                     const ImageMetadata& metadata) const override
    {
        return writePng(engine, execution, request, frame, metadata);
    }
};

} // namespace

Result<std::shared_ptr<const ImageCodec>> createPngCodec()
{
    return evaluateAcrossBoundary({"libpng", "createPngCodec", ErrorDomain::Io},
                                  [] { return std::shared_ptr<const ImageCodec>{std::make_shared<PngCodec>()}; });
}

namespace dependency_detail
{

void reportLibPng(DependencyVersion& entry) noexcept
{
    entry.name = "libpng";
    entry.linkage = DependencyLinkage::SharedLibrary;
    setVersionText(entry.compiled, PNG_LIBPNG_VER_STRING);
    // The accessor takes a structure only so that a caller inside a read or write can ask without
    // one; a null pointer is the documented way to ask about the library itself.
    setVersionText(entry.loaded, png_get_libpng_ver(nullptr));
}

} // namespace dependency_detail

} // namespace an
