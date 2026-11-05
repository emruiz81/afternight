// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A fixed-size, versioned byte record of one frame's drizzle geometry, for a caller that persists
/// mapped samples and must prove, before reusing them, that they were mapped by the geometry it is
/// asking for now.
///
/// `encodeDrizzleMapIdentity` writes every fact `DrizzleMapDescriptor` holds, and the pixel grid a
/// persisted normalization was evaluated on, into one canonical 2048-byte record.
/// `validateDrizzleMapIdentity` encodes the current geometry the same way and compares the whole
/// record, padding included. The comparison is exact rather than a short hash, so two different
/// geometries never validate against each other. It covers geometry only: source revisions,
/// validity, the kernel and other parameters belong in the owning consumer's larger key.
#pragma once

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapDescriptor.h>

#include <array>
#include <cstddef>
#include <span>

namespace an
{
/// Pixel grid in which a disk history's normalization was evaluated; always supplied explicitly.
enum class DrizzleNormalizationSpace : std::uint8_t
{
    SourcePixels, ///< Normalization evaluated before source-to-destination mapping.
    OutputPixels ///< Normalization evaluated on the mapped destination grid.
};

/// Fixed-size versioned wire identity: an eight-byte signature, the format version, the
/// normalization grid, the coordinate and invalid-coordinate conventions, both extents, the scale,
/// the registration's linear kind, matrix, polynomial coefficients and polynomial normalization, the
/// post-translation, and the source SIP's presence, reference pixel and every forward and inverse
/// coefficient. Integers and IEEE-754 double bits are little-endian, and every byte after the last
/// field is zero. No native ABI, locale-dependent formatting or truncated hash participates. The
/// caller persists these bytes with its artifact transaction and validates them before reusing
/// mapped or disk-history samples.
struct DrizzleMapIdentity
{
    std::array<std::byte, 2048u> bytes{}; ///< Complete record, including version and zero padding.
};

/// Encodes exact geometry identity, allocation-free on success and safe concurrently.
/// @param descriptor validated geometry; all stored coefficients, including signed zeros, are retained.
/// @param normalization explicit pixel grid of the persisted normalization.
/// @return the complete record, or `ErrorCode::InvalidArgument` for an unknown normalization grid.
[[nodiscard]] AFTERNIGHTENGINE_API Result<DrizzleMapIdentity>
encodeDrizzleMapIdentity(const DrizzleMapDescriptor& descriptor, DrizzleNormalizationSpace normalization) noexcept;

/// Validates persisted identity against the geometry and normalization currently requested.
/// This is geometry identity only; callers additionally key source revisions, masks and kernel options.
/// Allocation-free on success and thread-safe. Diagnostic allocation failure returns `AllocationFailed`.
/// @param descriptor current validated geometry.
/// @param normalization current explicit normalization grid.
/// @param stored exact persisted record, including its reserved bytes.
/// @return success; `ErrorCode::InvalidData` when `stored` is not the record's size;
///         `ErrorCode::InvalidArgument` for an unknown normalization grid; or
///         `ErrorCode::IncompatibleArguments` when any byte differs, including the version and the
///         zero padding.
[[nodiscard]] AFTERNIGHTENGINE_API Status validateDrizzleMapIdentity(const DrizzleMapDescriptor& descriptor,
                                                                     DrizzleNormalizationSpace normalization,
                                                                     std::span<const std::byte> stored) noexcept;
} // namespace an
