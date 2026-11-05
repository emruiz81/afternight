// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// SHA-256, for the identities the fixture layers record: a description's canonical text, a sample set, and a
// captured result. The neutral layer carries its own implementation because it may link nothing that knows an
// engine.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace an::tests::fixtures
{

/// An incremental SHA-256 (FIPS 180-4).
class FixtureDigest
{
public:
    FixtureDigest() noexcept;

    /// Adds bytes to the message.
    void update(std::span<const std::byte> bytes) noexcept;
    void update(std::string_view text) noexcept;

    /// The digest of everything added, as lowercase hexadecimal. Ends the message: a later update starts nothing.
    [[nodiscard]] std::string finish() noexcept;

private:
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> m_state{};
    std::array<std::uint8_t, 64> m_block{};
    std::size_t m_blockUsed = 0u;
    std::uint64_t m_totalBytes = 0u;
};

/// The SHA-256 of some bytes or some text, as lowercase hexadecimal.
[[nodiscard]] std::string sha256Hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256Hex(std::string_view text);

} // namespace an::tests::fixtures
