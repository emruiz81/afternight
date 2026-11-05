// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the camera RAW adapter's private LibRaw session and the steps it is read
// through.
//
// One `RawFile` is one open source for one read: LibRaw's callbacks hold its address, so it lives
// behind a `unique_ptr` and is never shared between executions. The steps run in a fixed order -
// open, describe, unpack, capture the metadata, extract - and the description is taken again after
// unpacking so that a decoder that changed the geometry is caught before the preallocated frame is
// written.

#pragma once

#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#if defined(_MSC_VER)
// LibRaw exports standard-library members; suppress only its own DLL-interface diagnostics.
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif
#if defined(AFTERNIGHT_HAS_RAWSPEED)
#include <afternight-libraw/libraw/libraw.h>
#else
#include <libraw/libraw.h>
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

namespace an
{
class EngineContext;
class ExecutionContext;
/// The exception boundary every LibRaw call runs inside; it names the library and the operation in a
/// translated failure.
inline constexpr ExternalBoundary kRawBoundary{"LibRaw", "decode camera RAW", ErrorDomain::Io};

/// Exposes LibRaw's extension hooks only inside the decoder boundary.
class RawDecoder final : public LibRaw
{
public:
    using LibRaw::adjust_bl;
    using LibRaw::is_phaseone_compressed;
};

/// Stable address for callbacks; one instance per open source, never shared by executions.
struct RawFile
{
    /// The operation the file is read for; its cancellation answers LibRaw's progress callback.
    const ExecutionContext* execution = nullptr;
    /// Set by LibRaw's data-error callback; a call that reports success with this set still fails as
    /// damaged data.
    std::atomic<bool> corrupt{false};
    /// The configuration this read uses, with the request's decoder override already applied.
    RawCodecOptions options;
    /// The accelerator phase last traced, so a checkpoint is traced whenever the phase changes.
    std::uint32_t reportedPhase = 0u;
    RawDecoder decoder; ///< The LibRaw instance, owned by this read alone.
};

/// Opens the literal path with operation-local callbacks and immutable decoding options.
Result<std::unique_ptr<RawFile>> openRaw(const ExecutionContext& execution, const CodecReadRequest& request,
                                         const RawCodecOptions& options);
/// Maps both return codes and recoverable data-error callbacks to a typed refusal.
Status rawStatus(const RawFile& file, int code, CodecFailure reason, std::string_view path);
/// Selects a qualified decompressor and, only for recoverable failures, reopens native once.
Status unpackRaw(std::unique_ptr<RawFile>& file, const CodecReadRequest& request);
/// Full-colour sensors alone need colour processing; a monochrome sensor is not RGB.
bool rawFullColor(const LibRaw& decoder) noexcept;
/// Describes output before allocation, including the actual filter pattern in visible coordinates.
Result<CodecImageInfo> describeRaw(RawFile& file, const EngineContext& engine, const CodecReadRequest& request);
/// Copies calibration before any library processing mutates black levels or matrices, beside the Exif
/// cards the file stated.
Result<ImageMetadata> rawMetadata(const LibRaw& decoder, bool requested, bool decoded, std::vector<CodecCard> exif);
/// The Exif a camera file states, as cards of `CodecNamespace::CameraRaw`, read by the metadata
/// library from the whole file. Exif that library cannot read, or that one image cannot hold, is
/// reported to the execution's diagnostic sink as a warning and yields no card, because the file is
/// the RAW library's to refuse; a container whose Exif the metadata library does not read yields none.
std::vector<CodecCard> rawExifCards(const EngineContext& engine, const ExecutionContext& execution,
                                    std::string_view path);
} // namespace an
