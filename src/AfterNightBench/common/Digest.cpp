// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Digest.h"

#include <cstring>
#include <string>
#include <string_view>

#if defined(__x86_64__) || defined(_M_X64)
#define AN_BENCH_SHA_EXTENSIONS 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__clang__) || defined(__GNUC__)
#define AN_BENCH_SHA_TARGET __attribute__((target("sha,sse4.1,ssse3")))
#else
#define AN_BENCH_SHA_TARGET
#endif
#endif

namespace an::bench
{
namespace
{

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotateRight(std::uint32_t value, std::uint32_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

#if defined(AN_BENCH_SHA_EXTENSIONS)

// Whether the processor has the SHA extensions and the SSSE3/SSE4.1 shuffles they are used with.
bool detectShaExtensions() noexcept
{
#if defined(_MSC_VER)
    int registers[4] = {};
    __cpuid(registers, 0);
    if (registers[0] < 7)
        return false;
    __cpuidex(registers, 1, 0);
    const bool shuffles = (registers[2] & (1 << 9)) != 0 && (registers[2] & (1 << 19)) != 0;
    __cpuidex(registers, 7, 0);
    return shuffles && (registers[1] & (1 << 29)) != 0;
#else
    unsigned a = 0u, b = 0u, c = 0u, d = 0u;
    if (__get_cpuid_max(0u, nullptr) < 7u || !__get_cpuid(1u, &a, &b, &c, &d))
        return false;
    const bool shuffles = (c & bit_SSSE3) != 0u && (c & bit_SSE4_1) != 0u;
    __cpuid_count(7u, 0u, a, b, c, d);
    return shuffles && (b & bit_SHA) != 0u;
#endif
}

// Compresses `count` whole blocks with the SHA extensions. The state is kept as the ABEF/CDGH
// halves the instructions work on; each group of four rounds adds its round constants, runs two
// double rounds, and advances the message schedule exactly as the portable loop defines it.
AN_BENCH_SHA_TARGET void compressWithExtensions(std::uint32_t state[8], const std::uint8_t* blocks,
                                                std::size_t count) noexcept
{
    const __m128i byteSwap = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);
    __m128i low = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[0]));
    __m128i high = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state[4]));
    low = _mm_shuffle_epi32(low, 0xB1); // CDAB
    high = _mm_shuffle_epi32(high, 0x1B); // EFGH
    __m128i abef = _mm_alignr_epi8(low, high, 8); // ABEF
    __m128i cdgh = _mm_blend_epi16(high, low, 0xF0); // CDGH
    for (std::size_t block = 0u; block < count; ++block, blocks += 64)
    {
        const __m128i savedAbef = abef;
        const __m128i savedCdgh = cdgh;
        __m128i message[4];
        for (int part = 0; part < 4; ++part)
            message[part] =
                _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 16 * part)), byteSwap);
        for (int group = 0; group < 16; ++group)
        {
            __m128i rounds = _mm_add_epi32(
                message[group & 3], _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kRoundConstants[4 * group])));
            cdgh = _mm_sha256rnds2_epu32(cdgh, abef, rounds);
            if (group >= 3 && group < 15)
            {
                const __m128i shifted = _mm_alignr_epi8(message[group & 3], message[(group - 1) & 3], 4);
                message[(group + 1) & 3] = _mm_add_epi32(message[(group + 1) & 3], shifted);
                message[(group + 1) & 3] = _mm_sha256msg2_epu32(message[(group + 1) & 3], message[group & 3]);
            }
            rounds = _mm_shuffle_epi32(rounds, 0x0E);
            abef = _mm_sha256rnds2_epu32(abef, cdgh, rounds);
            if (group >= 1 && group < 13)
                message[(group - 1) & 3] = _mm_sha256msg1_epu32(message[(group - 1) & 3], message[group & 3]);
        }
        abef = _mm_add_epi32(abef, savedAbef);
        cdgh = _mm_add_epi32(cdgh, savedCdgh);
    }
    const __m128i feba = _mm_shuffle_epi32(abef, 0x1B); // FEBA
    const __m128i dchg = _mm_shuffle_epi32(cdgh, 0xB1); // DCHG
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[0]), _mm_blend_epi16(feba, dchg, 0xF0)); // DCBA
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&state[4]), _mm_alignr_epi8(dchg, feba, 8)); // HGFE
}

#endif

} // namespace

bool Sha256::processorExtensionsAvailable() noexcept
{
#if defined(AN_BENCH_SHA_EXTENSIONS)
    static const bool available = detectShaExtensions();
    return available;
#else
    return false;
#endif
}

Sha256::Sha256(Sha256Implementation implementation)
    : m_state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
      m_bitCount(0), m_buffer{}, m_bufferSize(0),
      m_processorExtensions(implementation == Sha256Implementation::Automatic && processorExtensionsAvailable())
{
}

void Sha256::compressBlocks(const std::uint8_t* blocks, std::size_t count)
{
#if defined(AN_BENCH_SHA_EXTENSIONS)
    if (m_processorExtensions)
    {
        compressWithExtensions(m_state, blocks, count);
        return;
    }
#endif
    for (std::size_t block = 0u; block < count; ++block)
        compress(blocks + 64u * block);
}

void Sha256::compress(const std::uint8_t* block)
{
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

    for (int i = 0; i < 64; ++i)
    {
        const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
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

void Sha256::update(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_bitCount += static_cast<std::uint64_t>(size) * 8;

    // Fill the partial block first, then run whole blocks straight out of the caller's buffer.
    // This is what makes chunk boundaries irrelevant to the result.
    while (size > 0)
    {
        if (m_bufferSize == 0 && size >= 64)
        {
            const std::size_t whole = size / 64;
            compressBlocks(bytes, whole);
            bytes += 64 * whole;
            size -= 64 * whole;
            continue;
        }

        const std::size_t take = (std::min)(std::size_t{64} - m_bufferSize, size);
        std::memcpy(m_buffer + m_bufferSize, bytes, take);
        m_bufferSize += take;
        bytes += take;
        size -= take;

        if (m_bufferSize == 64)
        {
            compressBlocks(m_buffer, 1);
            m_bufferSize = 0;
        }
    }
}

std::string Sha256::hex()
{
    // Append 0x80, pad with zeros to 56 mod 64, then the big-endian bit count.
    const std::uint64_t bitCount = m_bitCount;

    std::uint8_t padding = 0x80;
    update(&padding, 1);
    m_bitCount = bitCount; // update() counted the padding; the length field must not include it.

    padding = 0x00;
    while (m_bufferSize != 56)
    {
        update(&padding, 1);
        m_bitCount = bitCount;
    }

    std::uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i)
    {
        lengthBytes[i] = static_cast<std::uint8_t>((bitCount >> (56 - i * 8)) & 0xFF);
    }
    std::memcpy(m_buffer + m_bufferSize, lengthBytes, 8);
    compressBlocks(m_buffer, 1);
    m_bufferSize = 0;

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : m_state)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            out.push_back(kHex[(word >> shift) & 0xF]);
        }
    }
    return out;
}

std::string Sha256::of(const void* data, std::size_t size)
{
    Sha256 hash;
    hash.update(data, size);
    return hash.hex();
}

std::string Sha256::of(const std::string& text)
{
    return of(text.data(), text.size());
}

std::string digestFixtureSet(const std::vector<std::string>& digests)
{
    Sha256 combined;
    constexpr std::string_view kDomain = "AfterNight benchmark fixture set v1\n";
    combined.update(kDomain.data(), kDomain.size());
    for (std::size_t index = 0; index < digests.size(); ++index)
    {
        const std::string entry =
            std::to_string(index) + ":" + std::to_string(digests[index].size()) + ":" + digests[index] + "\n";
        combined.update(entry.data(), entry.size());
    }
    return combined.hex();
}

} // namespace an::bench
