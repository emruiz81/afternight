// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - choosing the camera RAW decompressor and unpacking with it.
//
// A decoder mode goes to the RawSpeed accelerator only when LibRaw identifies it as one of the modes
// qualified exactly against LibRaw, and `Automatic` routes it there only where the accelerator was
// measured faster on this platform. A recoverable accelerator failure under `Automatic` is retried
// once through LibRaw on a freshly opened file, never on the partial decoder state; every fallback
// and its reason is traced. A build without the accelerator compiles only the native path.

#include "io/raw/RawFile.h"

#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <chrono>

namespace an
{
namespace
{
enum class FallbackReason : std::uint8_t
{
    None,
    UnqualifiedMode,
    BackendUnavailable,
    WorkspacePreflight,
    UnsupportedInput,
    MalformedInput,
    DecoderWarning,
    GeometryMismatch,
    HostNotQualified
};

void fallback(const ExecutionContext& execution, FallbackReason reason) noexcept
{
    execution.reportTrace(TraceEventKind::Counter, "codec.raw", "fallback.reason", static_cast<double>(reason),
                          "RawFallbackReason");
}

#if defined(AFTERNIGHT_HAS_RAWSPEED)
struct DecoderMode
{
    AnRawFamily family;
    std::uint32_t bits;
    std::uint32_t compression;
    std::uint32_t samples;
    std::uint32_t mode;
    bool automatic; // Whether Automatic routes the mode here, rather than only RequireRawSpeed.
};

// Family comes from LibRaw's actual decoder entry point, never an extension or maker string.
// The corpus qualifies compressed modes across camera models and sensor sizes. RawSpeed still
// checks its camera database, full decoded geometry and CFA before publishing any samples.
// Automatic routes a mode only where its timing met the criterion - a median at least 10% lower
// by more than three times both spreads - in warm and cold-file measurements on the nominated
// host. Every mode meets it on Windows. On Linux the Sony ARW2 mode gains under 11% warm and is
// slower from a cold file, so Automatic keeps it native there and RequireRawSpeed still reaches it.
#if defined(_WIN32)
constexpr bool kSonyArw2Automatic = true;
#else
constexpr bool kSonyArw2Automatic = false;
#endif

constexpr std::uint32_t losslessJpegMode(unsigned bits, unsigned components) noexcept
{
    return (bits << 16u) | (components << 8u) | 1u; // Lossless JPEG predictor 1.
}

constexpr DecoderMode kModes[]{
    {AnRawCanonCr2, 14u, 6u, 2u, losslessJpegMode(14u, 2u), true},
    {AnRawCanonCr2, 14u, 6u, 4u, losslessJpegMode(14u, 4u), true},
    {AnRawNikonNef, 12u, 34713u, 1u, 0x4420u, true}, // Nikon lossy integer curve.
    {AnRawNikonNef, 14u, 34713u, 1u, 0x4420u, true},
    {AnRawNikonNef, 12u, 34713u, 1u, 0x4630u, true}, // Nikon lossless integer curve.
    {AnRawNikonNef, 14u, 34713u, 1u, 0x4630u, true},
    {AnRawBayerDng, 16u, 7u, 1u, losslessJpegMode(12u, 2u), true},
    {AnRawBayerDng, 16u, 7u, 1u, losslessJpegMode(16u, 2u), true},
    {AnRawFujiRaf, 12u, 0u, 1u, 0u, true}, // Packed uncompressed X-Trans is also measurably faster.
    {AnRawSonyArw2, 14u, 32767u, 1u, 0u, kSonyArw2Automatic}};

bool isBayer(std::uint32_t filters) noexcept
{
    unsigned counts[3]{};
    for (unsigned y = 0u; y < 8u; ++y)
        for (unsigned x = 0u; x < 2u; ++x)
        {
            const auto slot = (filters >> (y * 4u + x * 2u)) & 3u;
            const auto first = (filters >> ((y % 2u) * 4u + x * 2u)) & 3u;
            const auto color = slot == 3u ? 1u : slot;
            if (color != (first == 3u ? 1u : first))
                return false;
            if (y < 2u)
                ++counts[color];
        }
    return counts[0] == 1u && counts[1] == 2u && counts[2] == 1u;
}

const DecoderMode* supportedMode(const AnRawIdentification& identified) noexcept
{
    if (!identified.supported || identified.rawCount != 1u || identified.imageIndex != 0u ||
        identified.sampleFormat != 0u || identified.predictor != 0u || identified.curveEntries != 0u)
        return nullptr;
    if (identified.width == 0u || identified.height == 0u || identified.width > 65535u || identified.height > 65535u ||
        identified.visibleWidth == 0u || identified.visibleHeight == 0u || identified.leftMargin > identified.width ||
        identified.topMargin > identified.height ||
        identified.visibleWidth > identified.width - identified.leftMargin ||
        identified.visibleHeight > identified.height - identified.topMargin)
        return nullptr;
    if (identified.family == AnRawFujiRaf ? identified.filters != 9u : !isBayer(identified.filters))
        return nullptr;
    if (identified.family == AnRawBayerDng)
    {
        // Tiled lossless JPEG is qualified independently of the camera and individual tile size.
        if (identified.tileWidth == 0u || identified.tileHeight == 0u || identified.tileWidth > 65535u ||
            identified.tileHeight > 65535u)
            return nullptr;
    }
    else if (identified.tileWidth != 2147483647u || identified.tileHeight != 2147483647u)
        return nullptr;
    for (const auto& mode : kModes)
        if (mode.family == identified.family && mode.bits == identified.bits &&
            mode.compression == identified.compression && mode.samples == identified.samples &&
            mode.mode == identified.mode)
            return &mode;
    return nullptr;
}

int stop(void* state, std::uint32_t phase, std::uint64_t completed) noexcept
{
    auto& file = *static_cast<RawFile*>(state);
    const auto& execution = *file.execution;
    // Checkpoint traces are deliberately sparse. They prove work inside a phase, while each
    // decoder row still checks the stop token even when no trace is delivered.
    if (phase != file.reportedPhase || (completed != 0u && completed % 128u == 0u))
    {
        const char* name = phase == 2u   ? "rawspeed.unpack.checkpoint"
                           : phase == 3u ? "rawspeed.correction.checkpoint"
                                         : "rawspeed.boundary.checkpoint";
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", name, static_cast<double>(completed),
                              "checkpoints");
        file.reportedPhase = phase;
    }
    return execution.checkCancellation().failed() ? 1 : 0;
}

FallbackReason retryReason(std::uint32_t outcome) noexcept
{
    switch (outcome)
    {
    case AnRawUnsupported:
        return FallbackReason::UnsupportedInput;
    case AnRawMalformed:
        return FallbackReason::MalformedInput;
    case AnRawWarnings:
        return FallbackReason::DecoderWarning;
    case AnRawGeometryMismatch:
        return FallbackReason::GeometryMismatch;
    case AnRawMemoryRefused:
        return FallbackReason::WorkspacePreflight;
    default:
        return FallbackReason::None;
    }
}
#endif
} // namespace

Status unpackRaw(std::unique_ptr<RawFile>& file, const CodecReadRequest& request)
{
    const auto& execution = *file->execution;
    const auto preference = file->options.decoderPreference;
    if (const auto stopped = execution.checkCancellation(); stopped.failed())
        return stopped;
    bool accelerate = false;
#if defined(AFTERNIGHT_HAS_RAWSPEED)
    AnRawIdentification identified{};
    identified.size = sizeof(identified);
    identified.version = 1u;
    if (preference != RawDecoderPreference::NativeLibRaw)
    {
        const auto code = anLibRawIdentify(&file->decoder, &identified);
        if (code != LIBRAW_SUCCESS)
            return rawStatus(*file, code, CodecFailure::MetadataUnreadable, request.path);
    }
    const DecoderMode* const mode = supportedMode(identified);
    const bool qualified = mode != nullptr;
    accelerate = preference != RawDecoderPreference::NativeLibRaw && qualified &&
                 (mode->automatic || preference == RawDecoderPreference::RequireRawSpeed);
    if (preference == RawDecoderPreference::RequireRawSpeed && !qualified)
        return Status::failure(ErrorCode::UnsupportedBackend);
    if (preference == RawDecoderPreference::Automatic && !accelerate)
        fallback(execution, qualified ? FallbackReason::HostNotQualified : FallbackReason::UnqualifiedMode);
    if (accelerate)
    {
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "external.workspace.estimate",
                              static_cast<double>(identified.workspaceEstimate), "bytes");
        if (identified.workspaceEstimate >
            static_cast<std::uint64_t>(file->options.libraryMemoryLimitMegabytes) * 1024u * 1024u)
        {
            if (preference == RawDecoderPreference::RequireRawSpeed)
                return rawStatus(*file, LIBRAW_TOO_BIG, CodecFailure::PayloadUnreadable, request.path);
            fallback(execution, FallbackReason::WorkspacePreflight);
            accelerate = false;
        }
    }
    if (accelerate)
    {
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "decoder.family", identified.family, "AnRawFamily");
        execution.reportTrace(TraceEventKind::Instant, "codec.raw",
                              "rawspeed.de70ef5fbc62cde91009c8cff7a206272abe631e");
        AnRawOptions options{};
        options.size = sizeof(options);
        options.version = 1u;
        options.family = identified.family;
        options.enabled = 1u;
        options.stop = stop;
        options.state = file.get();
        if (const int configured = anLibRawConfigure(&file->decoder, &options); configured != LIBRAW_SUCCESS)
            return codecFailure(ErrorCode::ExternalFailure, CodecFailure::PayloadUnreadable, CodecFormat::Raw,
                                kRawBoundary.operation, request.path,
                                translateExternalCode(kRawBoundary, ErrorCode::ExternalFailure, configured,
                                                      "the RawSpeed bridge rejected the decoder ABI or configuration"));
    }
#else
    if (preference == RawDecoderPreference::RequireRawSpeed)
        return Status::failure(ErrorCode::UnsupportedBackend);
    if (preference == RawDecoderPreference::Automatic)
        fallback(execution, FallbackReason::BackendUnavailable);
#endif
    execution.reportTrace(TraceEventKind::Instant, "codec.raw", accelerate ? "attempt.rawspeed" : "attempt.native");
    const auto started = std::chrono::steady_clock::now();
    const int code = file->decoder.unpack();
    execution.reportTrace(TraceEventKind::Counter, "codec.raw", "unpack",
                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
                          "ms");
    const auto checked = rawStatus(*file, code, CodecFailure::PayloadUnreadable, request.path);
#if defined(AFTERNIGHT_HAS_RAWSPEED)
    if (accelerate)
    {
        AnRawResult actual{};
        actual.size = sizeof(actual);
        actual.version = 1u;
        if (anLibRawResult(&file->decoder, &actual) != LIBRAW_SUCCESS)
            return Status::failure(ErrorCode::ExternalFailure);
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "correction.revision", actual.correctionRevision,
                              "revision");
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "encoded.read", actual.readMicroseconds / 1000.0,
                              "ms");
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "decoder.unpack",
                              actual.unpackMicroseconds / 1000.0, "ms");
        execution.reportTrace(TraceEventKind::Counter, "codec.raw", "decoder.correction",
                              actual.correctionMicroseconds / 1000.0, "ms");
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        if (checked.failed())
        {
            const auto reason = retryReason(actual.outcome);
            if (preference != RawDecoderPreference::Automatic || reason == FallbackReason::None)
                return checked;
            auto options = file->options;
            options.decoderPreference = RawDecoderPreference::NativeLibRaw;
            file.reset();
            fallback(execution, reason);
            auto reopened = openRaw(execution, request, options);
            if (!reopened.hasValue())
                return reopened.status();
            file = std::move(*reopened);
            return unpackRaw(file, request);
        }
        if (actual.outcome != AnRawAccelerated)
            return Status::failure(ErrorCode::UnsupportedBackend);
    }
#endif
    if (checked.failed())
        return checked;
    execution.reportTrace(TraceEventKind::Instant, "codec.raw", accelerate ? "selected.rawspeed" : "selected.native");
    return {};
}
} // namespace an
