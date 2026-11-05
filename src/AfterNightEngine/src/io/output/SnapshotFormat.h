// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the image snapshot's wire format: its fixed header, the encoding of its metadata section,
// and the checksum every section carries.
//
// Everything here is a pure function of bytes and values, with no file and no engine context, so the I/O in
// `ImageSnapshot.cpp` reasons about transfers and failures and this unit about what the bytes mean. Every field is
// written one by one in little-endian order and every enumeration is pinned to its wire code at compile time, so
// neither the host's byte order, nor a C++ layout, nor a reordered enumeration is part of the format.

#pragma once

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/ImageSnapshot.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// The size of the header, in bytes, the same in every version.
inline constexpr std::size_t kSnapshotHeaderBytes = 256u;

/// The first eight bytes of every snapshot.
inline constexpr std::array<std::byte, 8u> kSnapshotMagic{std::byte{'A'},  std::byte{'N'}, std::byte{'S'},
                                                          std::byte{'N'},  std::byte{'A'}, std::byte{'P'},
                                                          std::byte{'\r'}, std::byte{'\n'}};

/// Where the version field sits, so a reader can refuse an unknown version before reading anything else.
inline constexpr std::size_t kSnapshotVersionOffset = 8u;

/// The version this build writes, the published `kImageSnapshotFormatVersion`; every version before it is read too.
inline constexpr std::uint32_t kSnapshotVersion = kImageSnapshotFormatVersion;

/// A running XXH64 of seed zero over bytes fed in any number of pieces.
///
/// The published 64-bit xxHash: four lanes of multiply-rotate accumulation over 32-byte stripes, then the tail and
/// the avalanche. Feeding a span whole and feeding it in pieces give the same digest.
class SnapshotChecksum
{
public:
    /// Adds bytes to what has been hashed.
    ///
    /// @param bytes the next bytes, in order.
    void update(std::span<const std::byte> bytes) noexcept;

    /// The digest of everything added so far; the checksum may still be updated afterwards.
    ///
    /// @return the XXH64 of the bytes added, in order.
    [[nodiscard]] std::uint64_t digest() const noexcept;

private:
    /// The four lane accumulators.
    std::array<std::uint64_t, 4u> m_lanes{};

    /// Bytes added that do not yet fill a stripe.
    std::array<std::byte, 32u> m_pending{};

    /// How many bytes of `m_pending` are in use.
    std::size_t m_pendingBytes = 0u;

    /// How many bytes have been added in all.
    std::uint64_t m_total = 0u;

    /// Whether the lanes have been seeded.
    bool m_started = false;
};

/// The XXH64 of one span.
///
/// @param bytes the bytes to hash.
/// @return their digest.
[[nodiscard]] std::uint64_t snapshotChecksum(std::span<const std::byte> bytes) noexcept;

/// The length of the blocks the sample section is hashed in, so a read can verify blocks on several workers.
inline constexpr std::uint64_t kSnapshotHashBlockBytes = 4u * 1024u * 1024u;

/// The sample section's checksum from its blocks' digests: the XXH64 of the digests, each eight little-endian
/// bytes, in the blocks' order.
///
/// @param digests the XXH64 of each `kSnapshotHashBlockBytes` block of the section, the last one shorter.
/// @return the section's checksum.
[[nodiscard]] std::uint64_t combineSnapshotBlockDigests(std::span<const std::uint64_t> digests) noexcept;

/// A running block-wise checksum of a sample section fed in pieces of any length.
///
/// Pieces are split at the section's block boundaries, so the answer is the one `combineSnapshotBlockDigests`
/// gives over the blocks however the section was fed.
class SnapshotBlockChecksum
{
public:
    /// Adds the section's next bytes.
    ///
    /// @param bytes the next bytes, in order.
    void update(std::span<const std::byte> bytes);

    /// The checksum of the section fed so far, its last block possibly short.
    ///
    /// @return the section's checksum.
    [[nodiscard]] std::uint64_t digest() const;

private:
    /// The block being fed.
    SnapshotChecksum m_block{};

    /// How many bytes of the block being fed have been added.
    std::uint64_t m_inBlock = 0u;

    /// The digests of the blocks already complete.
    std::vector<std::uint64_t> m_digests{};
};

/// What a snapshot's header states.
///
/// The descriptor's byte order is the host's when a reader returns it; a writer's descriptor may name the host's
/// order or little-endian, which are the same on every platform this engine builds for. The layout is packed: no
/// row or plane padding.
struct SnapshotHeader
{
    /// The version the file states: 1 or 2.
    std::uint32_t version = kSnapshotVersion;

    /// The file's size in bytes.
    std::uint64_t fileBytes;

    /// The metadata section's length in bytes; it starts right after the header.
    std::uint64_t metadataBytes;

    /// The sample section's length in bytes; it starts right after the metadata.
    std::uint64_t sampleBytes;

    /// The validity section's length in bytes, zero when there is no mask; it starts right after the samples.
    std::uint64_t validityBytes;

    /// What the samples are.
    RawFrameDescriptor descriptor;

    /// Where the samples sit, packed.
    ImageLayout layout;

    /// How many bit-planes the mask has, zero when there is none.
    std::uint32_t validityPlanes;

    /// The metadata section's XXH64.
    std::uint64_t metadataChecksum;

    /// The sample section's block-wise checksum; see `combineSnapshotBlockDigests`.
    std::uint64_t sampleChecksum;

    /// The validity section's XXH64, zero when there is no mask.
    std::uint64_t validityChecksum;
};

/// The offset of the sample section.
///
/// @param header the header.
/// @return the header's size plus the metadata's.
[[nodiscard]] constexpr std::uint64_t snapshotSampleOffset(const SnapshotHeader& header) noexcept
{
    return kSnapshotHeaderBytes + header.metadataBytes;
}

/// The offset of the validity section.
///
/// @param header the header.
/// @return the sample section's offset plus its length, or zero when there is no mask.
[[nodiscard]] constexpr std::uint64_t snapshotValidityOffset(const SnapshotHeader& header) noexcept
{
    return header.validityPlanes == 0u ? 0u : snapshotSampleOffset(header) + header.sampleBytes;
}

/// The header's bytes, its own checksum included.
///
/// @param header what to state; its sections and geometry must already agree.
/// @return the 256 bytes.
[[nodiscard]] std::array<std::byte, kSnapshotHeaderBytes> encodeSnapshotHeader(const SnapshotHeader& header) noexcept;

/// Reads a header of a version this build reads and checks it against the file it came from.
///
/// The version must already have been read as one this build reads. The header size, the checksum, the reserved bytes,
/// every offset and length against the geometry stated, the descriptor, the layout and the mask planes are checked.
///
/// @param bytes the first 256 bytes of the file.
/// @param fileBytes the file's actual size.
/// @param limits the reading context's limits.
/// @return the header; `ErrorCode::InvalidData` naming what is wrong for anything malformed; or
///         `ErrorCode::LimitExceeded` for an extent or a layout beyond the limits.
[[nodiscard]] Result<SnapshotHeader> parseSnapshotHeader(std::span<const std::byte, kSnapshotHeaderBytes> bytes,
                                                         std::uint64_t fileBytes, const EngineLimits& limits);

/// How many bytes a metadata section encodes to.
///
/// @param identity the identity the snapshot is written for.
/// @param metadata the metadata it holds.
/// @return the exact length `encodeSnapshotMetadata` writes.
[[nodiscard]] std::uint64_t encodedSnapshotMetadataBytes(const ArtifactIdentity& identity,
                                                         const ImageMetadata& metadata) noexcept;

/// Encodes a metadata section into exactly as many bytes as `encodedSnapshotMetadataBytes` gives.
///
/// @param identity the identity the snapshot is written for.
/// @param metadata the metadata it holds.
/// @param destination where to write; exactly the encoded length.
void encodeSnapshotMetadata(const ArtifactIdentity& identity, const ImageMetadata& metadata,
                            std::span<std::byte> destination) noexcept;

/// A metadata section, decoded.
struct DecodedSnapshotMetadata
{
    /// The identity the snapshot was written for.
    ArtifactIdentity identity;

    /// The metadata, every object rebuilt through its own `create`.
    ImageMetadata metadata;
};

/// Decodes a metadata section in the layout of one version of the format.
///
/// Every byte must be consumed, every presence and logical byte must be zero or one, every enumeration a value
/// of its type, every count within what the remaining bytes could hold, and every object one its own `create`
/// accepts.
///
/// @param section the section's bytes.
/// @param version the version the file states, 1 or 2.
/// @return the identity and the metadata; or `ErrorCode::InvalidData` naming what could not be decoded.
[[nodiscard]] Result<DecodedSnapshotMetadata> decodeSnapshotMetadata(std::span<const std::byte> section,
                                                                     std::uint32_t version = kSnapshotVersion);

} // namespace an
