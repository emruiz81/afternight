// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureDigest.h"

#include <cstring>

namespace an::tests::fixtures
{

namespace
{

constexpr std::uint32_t kRounds[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

[[nodiscard]] constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned count) noexcept
{
    return (value >> count) | (value << (32u - count));
}

} // namespace

FixtureDigest::FixtureDigest() noexcept
    : m_state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u}
{
}

void FixtureDigest::update(std::span<const std::byte> bytes) noexcept
{
    for (const std::byte value : bytes)
    {
        m_block[m_blockUsed++] = static_cast<std::uint8_t>(value);
        if (m_blockUsed == m_block.size())
        {
            compress(m_block.data());
            m_blockUsed = 0u;
        }
    }
    m_totalBytes += bytes.size();
}

void FixtureDigest::update(std::string_view text) noexcept
{
    update(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

std::string FixtureDigest::finish() noexcept
{
    const std::uint64_t totalBits = m_totalBytes * 8u;
    m_block[m_blockUsed++] = 0x80u;
    if (m_blockUsed > 56u)
    {
        std::memset(m_block.data() + m_blockUsed, 0, m_block.size() - m_blockUsed);
        compress(m_block.data());
        m_blockUsed = 0u;
    }
    std::memset(m_block.data() + m_blockUsed, 0, 56u - m_blockUsed);
    for (unsigned index = 0u; index < 8u; ++index)
    {
        m_block[56u + index] = static_cast<std::uint8_t>(totalBits >> (56u - 8u * index));
    }
    compress(m_block.data());
    m_blockUsed = 0u;

    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(64u);
    for (const std::uint32_t word : m_state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            text.push_back(kHex[(word >> shift) & 0xFu]);
        }
    }
    return text;
}

void FixtureDigest::compress(const std::uint8_t* block) noexcept
{
    std::uint32_t schedule[64];
    for (unsigned index = 0u; index < 16u; ++index)
    {
        schedule[index] = (static_cast<std::uint32_t>(block[4u * index]) << 24u) |
                          (static_cast<std::uint32_t>(block[4u * index + 1u]) << 16u) |
                          (static_cast<std::uint32_t>(block[4u * index + 2u]) << 8u) |
                          static_cast<std::uint32_t>(block[4u * index + 3u]);
    }
    for (unsigned index = 16u; index < 64u; ++index)
    {
        const std::uint32_t low = rotateRight(schedule[index - 15u], 7u) ^ rotateRight(schedule[index - 15u], 18u) ^
                                  (schedule[index - 15u] >> 3u);
        const std::uint32_t high = rotateRight(schedule[index - 2u], 17u) ^ rotateRight(schedule[index - 2u], 19u) ^
                                   (schedule[index - 2u] >> 10u);
        schedule[index] = schedule[index - 16u] + low + schedule[index - 7u] + high;
    }
    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
    for (unsigned index = 0u; index < 64u; ++index)
    {
        const std::uint32_t sumE = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t first = h + sumE + choice + kRounds[index] + schedule[index];
        const std::uint32_t sumA = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t second = sumA + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

std::string sha256Hex(std::span<const std::byte> bytes)
{
    FixtureDigest digest;
    digest.update(bytes);
    return digest.finish();
}

std::string sha256Hex(std::string_view text)
{
    FixtureDigest digest;
    digest.update(text);
    return digest.finish();
}

} // namespace an::tests::fixtures
