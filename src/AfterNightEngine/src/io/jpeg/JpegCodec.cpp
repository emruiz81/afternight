// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the JPEG adapter: its capabilities, its prepared read, the factory, and the
// version report for the linked libjpeg-turbo.
//
// A prepared read opens the file and reads its header once, and decodes through the same session
// after the facade has checked that description; a region is refused as unsupported, because this
// adapter has no region reader. The capabilities are one representation, eight-bit unsigned, in one
// or three channels. The adapter keeps one thing between calls: the Exif encoding of its own software
// stamp, prepared once at creation, which a write with an intent reuses when it states no other Exif
// field and every read recognises by an exact byte match instead of decoding it again.

#include <AfterNightEngine/IO/JpegCodec.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/jpeg/JpegFile.h"
#include "io/registry/PreparedCodecRead.h"
#include "io/registry/PreparedCodecWrite.h"
#include "runtime/DependencyVersions.h"

#include <cstdint>
#include <utility>

namespace an
{
namespace
{

/// One open container, described once and decoded through the same session.
class JpegPreparedRead final : public PreparedCodecRead
{
public:
    /// Takes over an open session and the description read from it.
    ///
    /// @param session the open source.
    /// @param info what its header states.
    JpegPreparedRead(JpegReadHandle session, CodecImageInfo info)
        : m_session(std::move(session)), m_info(std::move(info))
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
        return readJpeg(engine, execution, request, *m_session, std::move(m_info));
    }

private:
    /// The open source, closed when this preparation is released.
    JpegReadHandle m_session;

    /// What its header states.
    CodecImageInfo m_info;
};

/// The adapter itself: immutable, holding only its software metadata template between calls.
class JpegCodec final : public PreparingImageCodec, public StreamingImageCodec
{
public:
    explicit JpegCodec(JpegSoftwareMetadata software) : m_software(std::move(software)) {}
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto session = openJpeg(request.path, request.readMetadata);
        if (!session.hasValue())
        {
            return session.status();
        }
        auto info = describeJpeg(**session, engine, request, m_software);
        if (!info.hasValue())
        {
            return info.status();
        }
        static_cast<void>(execution);
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<JpegPreparedRead>(std::move(*session), std::move(*info))};
    }

    CodecCapabilities capabilities() const override
    {
        // One representation, because a JPEG file states one: eight-bit samples, in one or three
        // components. A single-channel image is planar and an interleaved one is not, so both
        // arrangements are advertised for one decoded layout apiece.
        const auto samples = codecSampleBit({1u, RawSampleClass::UnsignedInteger});
        const auto layouts = static_cast<std::uint8_t>(codecLayoutBit(StorageArrangement::Planar) |
                                                       codecLayoutBit(StorageArrangement::Interleaved));
        return {CodecFormat::Jpeg, samples, samples, layouts, layouts, 3u, false};
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
        return writeJpeg(engine, execution, request, frame, metadata, m_software, nullptr);
    }

    Result<CodecEncodeReport> encodeTo(const EngineContext& engine, const ExecutionContext& execution,
                                       const CodecEncodeRequest& request, const RawFrame& frame,
                                       const ImageMetadata& metadata, CodecOutput& output) const override
    {
        return writeJpeg(engine, execution, request, frame, metadata, m_software, &output);
    }

private:
    JpegSoftwareMetadata m_software;
};

} // namespace

Result<std::shared_ptr<const ImageCodec>> createJpegCodec()
{
    auto created = evaluateAcrossBoundary(
        {"libjpeg-turbo", "createJpegCodec", ErrorDomain::Io}, []() -> Result<std::shared_ptr<const ImageCodec>> {
            auto software = prepareJpegSoftwareMetadata();
            if (!software.hasValue())
                return software.status();
            return std::shared_ptr<const ImageCodec>{std::make_shared<JpegCodec>(std::move(*software))};
        });
    return created.hasValue() ? std::move(*created) : Result<std::shared_ptr<const ImageCodec>>(created.status());
}

namespace dependency_detail
{

void reportLibJpeg(DependencyVersion& entry) noexcept
{
    entry.name = "libjpeg-turbo";
    entry.linkage = DependencyLinkage::SharedLibrary;

    // The packed number rather than the macro beside it, because that macro is an unquoted `3.2.0`
    // and turning it into text would mean a stringifying macro of this engine's own. The library
    // documents the packing as major * 1,000,000 + minor * 1,000 + patch.
    constexpr std::uint32_t kPacked = LIBJPEG_TURBO_VERSION_NUMBER;
    setVersionTextFromParts(entry.compiled, kPacked / 1000000u, (kPacked / 1000u) % 1000u, kPacked % 1000u);

    // No loaded version: the library's API exposes its version to a caller only through the
    // structures a compression or decompression object is initialised with, which is a version of
    // the interface rather than of the build. `build/scripts/tools/dependencies.txt` records that.
}

} // namespace dependency_detail

} // namespace an
