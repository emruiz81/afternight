// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading the Gaia container's little-endian fields out of borrowed bytes.
//
// The container is a fixed little-endian layout written by this project's own installer. Reading it
// by copying bytes into a packed structure would make the decode depend on this build's structure
// padding and on the host's byte order, neither of which the file says anything about. Reading it
// field by field costs nothing measurable next to the work of building an entry and means the same
// thing on every target.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an::catalog_detail
{

/// The unsigned 16-bit field at an offset.
///
/// @param bytes the block to read, which must hold two bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value.
[[nodiscard]] inline std::uint16_t readUint16LittleEndian(std::span<const std::byte> bytes,
                                                          const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(bytes[offset]) |
                                      (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u));
}

/// The unsigned 32-bit field at an offset.
///
/// @param bytes the block to read, which must hold four bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value.
[[nodiscard]] inline std::uint32_t readUint32LittleEndian(std::span<const std::byte> bytes,
                                                          const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

/// The unsigned 64-bit field at an offset.
///
/// @param bytes the block to read, which must hold eight bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value.
[[nodiscard]] inline std::uint64_t readUint64LittleEndian(std::span<const std::byte> bytes,
                                                          const std::size_t offset) noexcept
{
    return static_cast<std::uint64_t>(readUint32LittleEndian(bytes, offset)) |
           (static_cast<std::uint64_t>(readUint32LittleEndian(bytes, offset + 4u)) << 32u);
}

/// The signed 16-bit field at an offset, in two's complement.
///
/// @param bytes the block to read, which must hold two bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value.
[[nodiscard]] inline std::int16_t readInt16LittleEndian(std::span<const std::byte> bytes,
                                                        const std::size_t offset) noexcept
{
    return static_cast<std::int16_t>(readUint16LittleEndian(bytes, offset));
}

/// The 32-bit floating-point field at an offset.
///
/// @param bytes the block to read, which must hold four bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value, which may be any float including a not-a-number.
[[nodiscard]] inline float readFloat32LittleEndian(std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::bit_cast<float>(readUint32LittleEndian(bytes, offset));
}

/// The 64-bit floating-point field at an offset.
///
/// @param bytes the block to read, which must hold eight bytes at `offset`.
/// @param offset where the field begins.
/// @return the field's value, which may be any double including a not-a-number.
[[nodiscard]] inline double readFloat64LittleEndian(std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::bit_cast<double>(readUint64LittleEndian(bytes, offset));
}

} // namespace an::catalog_detail
