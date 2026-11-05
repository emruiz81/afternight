// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The camera RAW adapter: LibRaw behind the codec interface, with an optional qualified accelerator.
///
/// `createRawCodec` builds an adapter to register in a `CodecRegistry`. It probes and decodes whole
/// images; it has no encoder and no region reader, and nothing decodes a whole frame and crops it in
/// place of one. Each read owns its own LibRaw instance, so one adapter serves any number of engine
/// contexts and executions at once.
///
/// **What a decode returns depends on the sensor.** A Bayer or X-Trans mosaic, and a monochrome
/// sensor, is one plane of native-endian UInt16 camera codes with the black level subtracted and
/// nothing else applied: no white balance, no scaling to full range, no interpolation, no orientation
/// and no camera-to-RGB transform. The descriptor states the actual filter pattern, the whole 6x6
/// table for X-Trans. Only a full-colour sensor goes through LibRaw's processing, and its 16-bit linear
/// sRGB result is copied into three planes. The source's black and white levels, colour matrices,
/// gains and orientation travel in `AcquisitionMetadata::rawCamera` when metadata is requested.
///
/// **Nothing is white-balanced on ingest.** Demosaic produces camera-space RGB from an X-Trans frame
/// as from a Bayer one, and white balance follows it. A four-colour mosaic is refused rather than
/// carried.
///
/// **Memory outside the budget.** The decoded frame is allocated from the engine context before
/// unpacking and can be adopted without a copy, but the decoder's workspace - the accelerator's copy
/// of the encoded file included - is not counted by the engine and has no reliable peak bound; the
/// `libraryMemoryLimitMegabytes` guard does not make it one, and the accelerator's workspace
/// preflight is an estimate. A caller that needs a strict total memory bound must not register this
/// adapter. Every library allocation is released before the call returns.
///
/// **Decoder choice.** Where the build contains the RawSpeed accelerator, a decoder mode qualified
/// against LibRaw can be decoded by it; `RawDecoderPreference` chooses per adapter and
/// `CodecReadRequest::rawDecoderPreference` per read, and latency depends on the choice. The decoder
/// selected, the reason for any fallback and each phase's time go to the execution's trace sink.
/// Cancellation is observed through LibRaw's progress callback and, on the accelerated path, at rows,
/// tiles and correction chunks too; a cancellation or an allocation failure is never retried.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/RawDecoderPreference.h>

#include <cstdint>
#include <memory>

namespace an
{
class ImageCodec;

/// The white balance applied to a full-colour sensor; a mosaic always keeps camera-space samples.
enum class RawFullColorWhiteBalance : std::uint8_t
{
    None, ///< Equal channel multipliers.
    Camera, ///< The as-shot multipliers, falling back to LibRaw's daylight ones when the file has none.
    Daylight, ///< The decoder's daylight calibration.
    Automatic, ///< LibRaw's estimate from the scene.
    Custom ///< The caller's red, green and blue gains, from `RawCodecOptions::fullColorMultipliers`.
};

/// The configuration copied into a RAW adapter; each read keeps its own library state.
struct RawCodecOptions
{
    /// The white balance for a full-colour sensor; a mosaic ignores it.
    RawFullColorWhiteBalance fullColorWhiteBalance = RawFullColorWhiteBalance::None;
    /// The red, green and blue gains `Custom` applies: positive, and finite and nonzero as Float32.
    double fullColorMultipliers[3]{1.0, 1.0, 1.0};
    std::uint32_t libraryMemoryLimitMegabytes =
        512u; ///< LibRaw's own allocation guard in MiB, positive; not a bound on the decoder's peak.
    /// The adapter's default decoder choice; `CodecReadRequest::rawDecoderPreference` overrides it
    /// for one read.
    RawDecoderPreference decoderPreference = RawDecoderPreference::Automatic;
};

/// Creates the camera RAW adapter.
///
/// A read through it fails, besides the facade's own checks, with `ErrorCode::InvalidArgument` for a
/// decoder choice outside its enumeration, before the file is opened; `ErrorCode::OutOfRange` for an
/// `imageIndex` past the file's last image; `ErrorCode::Unsupported` with
/// `CodecFailure::UnsupportedRepresentation` for a filter array a descriptor cannot state;
/// `ErrorCode::UnsupportedBackend` when `RawDecoderPreference::RequireRawSpeed` finds no qualified
/// accelerator; or a LibRaw refusal - `CodecFailure::MetadataUnreadable` while opening,
/// `CodecFailure::PayloadUnreadable` while unpacking - classified as `ErrorCode::InvalidData` for
/// damaged data, including a data error reported through LibRaw's callback on a call that
/// succeeded, `ErrorCode::AllocationFailed` for its memory refusals, `ErrorCode::Unsupported` for a
/// file it does not support, `ErrorCode::LimitExceeded` for one too large, and
/// `ErrorCode::ExternalFailure` for a read error.
///
/// @param options the decoder configuration; copied, never borrowed.
/// @return the adapter; `ErrorCode::InvalidArgument` for a zero `libraryMemoryLimitMegabytes`, an
///         option outside its enumeration, or a `Custom` gain that is not positive, or not finite and
///         nonzero as Float32; or `ErrorCode::AllocationFailed` when the adapter cannot be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::shared_ptr<const ImageCodec>>
createRawCodec(const RawCodecOptions& options = {});
} // namespace an
