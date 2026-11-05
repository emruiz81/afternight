// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The calibration facts a camera raw file states, carried as a record and never applied.
///
/// A raw decoder knows the sensor's black offsets, its saturation code, its white-balance gains and
/// its colour matrices before it changes a single level. `RawCameraMetadata` holds those values as
/// the decoder states them - in original sensor codes, before any black subtraction, normalization
/// or colour transform - so that nothing a later stage computes can be mistaken for a source fact.
/// It travels in `AcquisitionMetadata::rawCamera`, which the camera raw codec in `IO/RawCodec.h`
/// fills when metadata is requested, and `AcquisitionMetadata::create` refuses a record that
/// `validateRawCameraMetadata` refuses.
///
/// **A record, not a policy.** No algorithm applies these values implicitly: white balance reads
/// them only from a record its caller passes (`Algorithms/Demosaic/CfaWhiteBalance.h`), and the
/// orientation is retained rather than applied on ingest.
///
/// Every four-entry array is in the decoder's channel order - red, green, blue, second green. An
/// absent value is spelled the way the decoder spells it: zero for a scalar, zero or below for a
/// gain. The two matrices are copied as the decoder states them, and nothing in the record marks one
/// as absent.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <vector>

namespace an
{
/// Camera calibration facts, as a raw decoder states them before decoding changes any level.
///
/// A plain aggregate whose members default to zero, false or empty. It travels inside the
/// acquisition record, and no member is ever applied implicitly by an algorithm.
struct RawCameraMetadata
{
    std::uint32_t blackLevel = 0u; ///< The black offset common to every channel, in original sensor codes.
    std::uint32_t channelBlack[4]{}; ///< Each channel's additional black offset, in original sensor codes.
    std::uint32_t whiteLevel = 0u; ///< The saturation code; zero means the decoder did not state one.
    std::uint32_t blackPatternRows = 0u; ///< The repeating black-offset tile's height; zero means absent.
    std::uint32_t blackPatternColumns = 0u; ///< The repeating black-offset tile's width; zero means absent.

    /// The repeating tile's additional black offsets, row-major, in original sensor codes. Holds
    /// exactly `blackPatternRows` times `blackPatternColumns` entries, and at most 4096.
    std::vector<std::uint32_t> blackPattern{};

    double cameraMultipliers[4]{}; ///< The as-shot white-balance gains; an entry of zero or below is absent.
    double daylightMultipliers[4]{}; ///< The decoder's daylight gains; an entry of zero or below is absent.
    double cameraToRgb[3][4]{}; ///< The decoder's camera-to-linear-sRGB matrix, before white balance.

    /// The decoder's matrix between camera and XYZ colour, four camera channels by three XYZ
    /// components, copied in the decoder's own orientation.
    double cameraToXyz[4][3]{};

    double isoSpeed = 0.0; ///< The ISO sensitivity setting; zero means absent. Not a gain in electrons.
    double focalLengthMillimeters = 0.0; ///< The lens focal length, in millimetres; zero means absent.
    double aperture = 0.0; ///< The lens f-number; zero means absent.

    /// The decoder's orientation code, retained as a fact and never applied on ingest.
    /// `imageOrientationFromRawFlip` in `Algorithms/Geometry/Geometry.h` reads it.
    int orientation = 0;

    int exifColorSpace = 0; ///< The Exif colour-space code the file states.
    bool asShotWhiteBalanceApplied = false; ///< Whether the source already applied as-shot white balance.

    /// Compares every source fact, including absent calibration values.
    /// @param left one record.
    /// @param right another record.
    /// @return true when all values agree.
    [[nodiscard]] friend bool operator==(const RawCameraMetadata& left, const RawCameraMetadata& right) = default;
};

/// Whether a record has the shape and the finite values a decoder's facts must have.
///
/// Shape and finiteness, not plausibility: the black-offset tile is absent in both directions or
/// present in both, and holds exactly rows times columns entries and at most 4096; the ISO speed,
/// focal length and aperture are finite and not negative; and every gain and matrix entry is finite.
/// A gain of zero or below is accepted, because that is how a decoder says it has none, and deciding
/// what to do without one belongs to whatever applies white balance.
///
/// Thread-safe and reentrant.
///
/// @param metadata the record to check, borrowed for the call.
/// @return success, or `ErrorCode::InvalidArgument` naming the rejected field in `ContextKey::Parameter`.
[[nodiscard]] AFTERNIGHTENGINE_API Status validateRawCameraMetadata(const RawCameraMetadata& metadata);
} // namespace an
