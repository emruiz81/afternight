// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// SHA-256, as FIPS 180-4 states it, over bytes handed over in any number of pieces.
///
/// The product digests what it publishes and what it installs - a master a few hundred megabytes long, a catalog of
/// gigabytes - so where the processor has the x86 SHA extensions the blocks are compressed by them, several times
/// faster than the portable rounds. Both give the standard's digest, so a recorded digest never depends on the
/// machine that took it.
#pragma once

#include <AfterNightPlatform/PlatformExport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace an::platform
{

/// Which block compression a digest runs.
enum class Sha256Implementation : std::uint8_t
{
    /// The instructions where the processor has them, otherwise the portable rounds.
    Automatic,
    /// The portable rounds, written as the standard states them.
    Portable,
    /// The x86 SHA extensions; the portable rounds on a processor without them.
    Instructions
};

/// Whether this processor runs the x86 SHA extensions and the SSSE3 and SSE4.1 instructions used beside them.
///
/// Asked of the processor at every call, so nothing is kept for the process.
///
/// @return false on a processor without them and on every other architecture.
[[nodiscard]] AFTERNIGHTPLATFORM_API bool sha256InstructionsAvailable() noexcept;

/// A SHA-256 digest taken over a message handed over in pieces.
///
/// One digest belongs to one thread at a time.
class Sha256
{
public:
    /// A digest of no bytes yet.
    ///
    /// @param implementation the compression to run; `Instructions` falls back to the portable rounds on a
    ///        processor without them.
    AFTERNIGHTPLATFORM_API explicit Sha256(
        Sha256Implementation implementation = Sha256Implementation::Automatic) noexcept;

    /// The compression this digest runs.
    ///
    /// @return `Portable` or `Instructions`, never `Automatic`.
    [[nodiscard]] AFTERNIGHTPLATFORM_API Sha256Implementation implementation() const noexcept;

    /// Adds the message's next bytes.
    ///
    /// @param bytes the bytes, read during the call only.
    AFTERNIGHTPLATFORM_API void update(std::span<const std::byte> bytes) noexcept;

    /// The digest of every byte added since the digest was made or last finished, which begins a new message.
    ///
    /// @return the 32 bytes of the digest, most significant first.
    [[nodiscard]] AFTERNIGHTPLATFORM_API std::array<std::uint8_t, 32> finish() noexcept;

private:
    /// Compresses whole 64-byte blocks into the state.
    void compress(const std::uint8_t* blocks, std::size_t count) noexcept;

    /// Begins a new message.
    void restart() noexcept;

    std::array<std::uint32_t, 8> m_state{}; ///< The eight words of the hash value, H0 to H7.
    std::array<std::uint8_t, 64> m_block{}; ///< The bytes of a block not yet complete.
    std::uint64_t m_length = 0u; ///< Every byte added to this message.
    std::size_t m_buffered = 0u; ///< How many bytes of `m_block` hold the message.
    Sha256Implementation m_implementation = Sha256Implementation::Portable; ///< The compression this digest runs.
};

/// The digest of a whole file, read in pieces of a mebibyte.
///
/// @param path the file.
/// @param implementation the compression to run.
/// @return the digest, or nothing for a file that cannot be opened or read to its end.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::optional<std::array<std::uint8_t, 32>>
sha256OfFile(const std::filesystem::path& path, Sha256Implementation implementation = Sha256Implementation::Automatic);

/// A digest as sixty-four lowercase hexadecimal digits, most significant byte first.
///
/// @param digest the digest.
/// @return the digits.
[[nodiscard]] AFTERNIGHTPLATFORM_API std::string sha256Hex(const std::array<std::uint8_t, 32>& digest);

} // namespace an::platform
