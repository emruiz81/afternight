// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the TIFF adapter: its capabilities, its prepared read, the factory, and the
// version report for the linked libtiff.
//
// A prepared read opens the container and describes the selected image once, and decodes through
// the same handle after the facade has checked that description; a region is refused as
// unsupported, because this adapter has no region reader. The capabilities are every width and class
// the format spells, in either arrangement and up to 65,535 channels, and the adapter holds nothing
// between calls.

#include <AfterNightEngine/IO/TiffCodec.h>

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/registry/PreparedCodecRead.h"
#include "io/tiff/TiffFile.h"
#include "runtime/DependencyVersions.h"

#include <utility>

namespace an
{
namespace
{

/// One open container, described once and decoded through the same handle.
class TiffPreparedRead final : public PreparedCodecRead
{
public:
    /// Takes over an open container and the description read from it.
    ///
    /// @param file the open source.
    /// @param info what the selected image states.
    TiffPreparedRead(TiffHandle file, CodecImageInfo info) : m_file(std::move(file)), m_info(std::move(info)) {}

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
        return readTiff(engine, execution, request, *m_file, std::move(m_info));
    }

private:
    /// The open source, closed when this preparation is released.
    TiffHandle m_file;

    /// What the selected image states.
    CodecImageInfo m_info;
};

/// The adapter itself: immutable, and holding nothing between calls.
class TiffCodec final : public PreparingImageCodec
{
public:
    Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                           const ExecutionContext& execution,
                                                           const CodecReadRequest& request) const override
    {
        auto file = openTiffRead(engine, execution, request.path);
        if (!file.hasValue())
        {
            return file.status();
        }
        auto info = describeTiff(**file, engine, execution, request);
        if (!info.hasValue())
        {
            return info.status();
        }
        return std::unique_ptr<PreparedCodecRead>{
            std::make_unique<TiffPreparedRead>(std::move(*file), std::move(*info))};
    }

    CodecCapabilities capabilities() const override
    {
        // Every width and class this container spells: the eight resident representations, the two
        // sixty-four-bit integers and the half-precision float, which are carried as exact
        // nonresident codes rather than narrowed. Both arrangements, because the container states
        // which one it holds and this adapter reports that rather than rewriting it.
        const std::uint32_t samples =
            codecSampleBit({1u, RawSampleClass::UnsignedInteger}) |
            codecSampleBit({2u, RawSampleClass::UnsignedInteger}) |
            codecSampleBit({4u, RawSampleClass::UnsignedInteger}) |
            codecSampleBit({8u, RawSampleClass::UnsignedInteger}) |
            codecSampleBit({1u, RawSampleClass::SignedInteger}) | codecSampleBit({2u, RawSampleClass::SignedInteger}) |
            codecSampleBit({4u, RawSampleClass::SignedInteger}) | codecSampleBit({8u, RawSampleClass::SignedInteger}) |
            codecSampleBit({2u, RawSampleClass::FloatingPoint}) | codecSampleBit({4u, RawSampleClass::FloatingPoint}) |
            codecSampleBit({8u, RawSampleClass::FloatingPoint});
        const auto layouts = static_cast<std::uint8_t>(codecLayoutBit(StorageArrangement::Planar) |
                                                       codecLayoutBit(StorageArrangement::Interleaved));
        return {CodecFormat::Tiff, samples, samples, layouts, layouts, 65535u, false};
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
        return writeTiff(engine, execution, request, frame, metadata);
    }
};

} // namespace

Result<std::shared_ptr<const ImageCodec>> createTiffCodec()
{
    return evaluateAcrossBoundary({"libtiff", "createTiffCodec", ErrorDomain::Io},
                                  [] { return std::shared_ptr<const ImageCodec>{std::make_shared<TiffCodec>()}; });
}

namespace dependency_detail
{

void reportLibTiff(DependencyVersion& entry) noexcept
{
    entry.name = "libtiff";
    entry.linkage = DependencyLinkage::SharedLibrary;

    // Both of libtiff's versions are the same multi-line copyright banner - the compiled one as a
    // macro, the loaded one from the library - and the same bounded scan reads the number out of both,
    // so a difference between them is a difference between the copies rather than between two ways
    // of asking.
    setVersionTextFromBanner(entry.compiled, TIFFLIB_VERSION_STR, "Version ");
    setVersionTextFromBanner(entry.loaded, TIFFGetVersion(), "Version ");
}

} // namespace dependency_detail

} // namespace an
