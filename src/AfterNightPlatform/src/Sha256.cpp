// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// SHA-256's block compression, as the standard's rounds and through the x86 SHA extensions.
///
/// The portable compression is FIPS 180-4's own: the message schedule, then sixty-four rounds over the eight working
/// variables. The instructions compute the same rounds two at a time: `sha256rnds2` reads and writes the state as
/// two vectors, the words A, B, E and F in one and C, D, G and H in the other, and `sha256msg1` and `sha256msg2`
/// extend the schedule four words at a time. The digest holds no state beyond its own members, and which
/// compression runs is decided when a digest is made, by asking the processor.

#include <AfterNightPlatform/Sha256.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#if defined(_WIN32)
#include <share.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define AN_PLATFORM_SHA_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#else
#define AN_PLATFORM_SHA_X86 0
#endif

// Clang and GCC compile an intrinsic only inside a function that names its instruction set; MSVC compiles any.
#if AN_PLATFORM_SHA_X86 && (defined(__clang__) || defined(__GNUC__))
#define AN_PLATFORM_SHA_TARGET __attribute__((target("sha,sse4.1,ssse3")))
#else
#define AN_PLATFORM_SHA_TARGET
#endif

namespace an::platform
{
namespace
{

/// The sixty-four round constants: the first thirty-two bits of the fractional parts of the cube roots of the first
/// sixty-four primes.
constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

/// The initial hash value: the first thirty-two bits of the fractional parts of the square roots of the first eight
/// primes.
constexpr std::array<std::uint32_t, 8> kInitialState{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

/// A word read most significant byte first, as the standard reads the message.
[[nodiscard]] std::uint32_t bigEndianWord(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) | (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) | static_cast<std::uint32_t>(bytes[3]);
}

/// The standard's compression of `count` blocks, round by round.
void compressPortable(std::array<std::uint32_t, 8>& state, const std::uint8_t* blocks, std::size_t count) noexcept
{
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t block = 0u; block < count; ++block, blocks += 64)
    {
        for (std::size_t t = 0u; t < 16u; ++t)
        {
            schedule[t] = bigEndianWord(blocks + 4u * t);
        }
        for (std::size_t t = 16u; t < 64u; ++t)
        {
            const std::uint32_t before2 = schedule[t - 2u];
            const std::uint32_t before15 = schedule[t - 15u];
            const std::uint32_t sigma1 = std::rotr(before2, 17) ^ std::rotr(before2, 19) ^ (before2 >> 10u);
            const std::uint32_t sigma0 = std::rotr(before15, 7) ^ std::rotr(before15, 18) ^ (before15 >> 3u);
            schedule[t] = sigma1 + schedule[t - 7u] + sigma0 + schedule[t - 16u];
        }
        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];
        for (std::size_t t = 0u; t < 64u; ++t)
        {
            const std::uint32_t bigSigma1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t first = h + bigSigma1 + choose + kRoundConstants[t] + schedule[t];
            const std::uint32_t bigSigma0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = bigSigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
}

#if AN_PLATFORM_SHA_X86

/// Four rounds: the two `sha256rnds2` steps over the four scheduled words beside their constants.
///
/// On entry `abef` holds A, B, E and F and `cdgh` holds C, D, G and H; two rounds later the first step's answer is
/// the new A, B, E and F and the old A, B, E and F are the new C, D, G and H, so after the second step the two
/// vectors hold the state four rounds on under the same names.
AN_PLATFORM_SHA_TARGET inline void fourRounds(__m128i& abef, __m128i& cdgh, __m128i message,
                                              const std::uint32_t* constants) noexcept
{
    const __m128i words = _mm_add_epi32(message, _mm_loadu_si128(reinterpret_cast<const __m128i*>(constants)));
    cdgh = _mm_sha256rnds2_epu32(cdgh, abef, words);
    abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(words, 0x0e));
}

/// The schedule's next four words from the sixteen before them, held as four vectors of four, oldest first:
/// `sigma1` of the words two back, the words seven back, `sigma0` of the words fifteen back and the words sixteen
/// back, summed.
AN_PLATFORM_SHA_TARGET inline __m128i nextWords(__m128i fourBack, __m128i threeBack, __m128i twoBack,
                                                __m128i oneBack) noexcept
{
    const __m128i partial =
        _mm_add_epi32(_mm_sha256msg1_epu32(fourBack, threeBack), _mm_alignr_epi8(oneBack, twoBack, 4));
    return _mm_sha256msg2_epu32(partial, oneBack);
}

/// Four words of a block, each read most significant byte first.
AN_PLATFORM_SHA_TARGET inline __m128i blockWords(const std::uint8_t* bytes) noexcept
{
    const __m128i byteOrder = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);
    return _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes)), byteOrder);
}

/// The compression of `count` blocks through the SHA extensions.
AN_PLATFORM_SHA_TARGET void compressInstructions(std::array<std::uint32_t, 8>& state, const std::uint8_t* blocks,
                                                 std::size_t count) noexcept
{
    // The state in memory is A to H; the instructions want A, B, E, F in one vector and C, D, G, H in the other,
    // each with its first word in the highest lane.
    const __m128i lower = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data())), 0xb1);
    const __m128i upper = _mm_shuffle_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(state.data() + 4)), 0x1b);
    __m128i abef = _mm_alignr_epi8(lower, upper, 8);
    __m128i cdgh = _mm_blend_epi16(upper, lower, 0xf0);
    for (std::size_t block = 0u; block < count; ++block, blocks += 64)
    {
        const __m128i abefBefore = abef;
        const __m128i cdghBefore = cdgh;
        __m128i words0 = blockWords(blocks);
        __m128i words1 = blockWords(blocks + 16);
        __m128i words2 = blockWords(blocks + 32);
        __m128i words3 = blockWords(blocks + 48);
        fourRounds(abef, cdgh, words0, kRoundConstants.data());
        fourRounds(abef, cdgh, words1, kRoundConstants.data() + 4);
        fourRounds(abef, cdgh, words2, kRoundConstants.data() + 8);
        fourRounds(abef, cdgh, words3, kRoundConstants.data() + 12);
        for (std::size_t round = 16u; round < 64u; round += 16u)
        {
            words0 = nextWords(words0, words1, words2, words3);
            fourRounds(abef, cdgh, words0, kRoundConstants.data() + round);
            words1 = nextWords(words1, words2, words3, words0);
            fourRounds(abef, cdgh, words1, kRoundConstants.data() + round + 4);
            words2 = nextWords(words2, words3, words0, words1);
            fourRounds(abef, cdgh, words2, kRoundConstants.data() + round + 8);
            words3 = nextWords(words3, words0, words1, words2);
            fourRounds(abef, cdgh, words3, kRoundConstants.data() + round + 12);
        }
        abef = _mm_add_epi32(abef, abefBefore);
        cdgh = _mm_add_epi32(cdgh, cdghBefore);
    }
    const __m128i reversed = _mm_shuffle_epi32(abef, 0x1b);
    const __m128i swapped = _mm_shuffle_epi32(cdgh, 0xb1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state.data()), _mm_blend_epi16(reversed, swapped, 0xf0));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state.data() + 4), _mm_alignr_epi8(swapped, reversed, 8));
}

#endif

} // namespace

bool sha256InstructionsAvailable() noexcept
{
#if AN_PLATFORM_SHA_X86
    constexpr std::uint32_t kSsse3 = 1u << 9u;
    constexpr std::uint32_t kSse41 = 1u << 19u;
    constexpr std::uint32_t kSha = 1u << 29u;
#if defined(_MSC_VER)
    int registers[4] = {};
    __cpuid(registers, 0);
    if (registers[0] < 7)
    {
        return false;
    }
    __cpuidex(registers, 1, 0);
    const auto features = static_cast<std::uint32_t>(registers[2]);
    __cpuidex(registers, 7, 0);
    const auto extended = static_cast<std::uint32_t>(registers[1]);
#else
    if (__get_cpuid_max(0u, nullptr) < 7u)
    {
        return false;
    }
    unsigned int eax = 0u;
    unsigned int ebx = 0u;
    unsigned int ecx = 0u;
    unsigned int edx = 0u;
    __get_cpuid(1u, &eax, &ebx, &ecx, &edx);
    const std::uint32_t features = ecx;
    __get_cpuid_count(7u, 0u, &eax, &ebx, &ecx, &edx);
    const std::uint32_t extended = ebx;
#endif
    return (features & kSsse3) != 0u && (features & kSse41) != 0u && (extended & kSha) != 0u;
#else
    return false;
#endif
}

Sha256::Sha256(Sha256Implementation implementation) noexcept
    : m_implementation(implementation != Sha256Implementation::Portable && sha256InstructionsAvailable()
                           ? Sha256Implementation::Instructions
                           : Sha256Implementation::Portable)
{
    restart();
}

Sha256Implementation Sha256::implementation() const noexcept
{
    return m_implementation;
}

void Sha256::restart() noexcept
{
    m_state = kInitialState;
    m_length = 0u;
    m_buffered = 0u;
}

void Sha256::compress(const std::uint8_t* blocks, std::size_t count) noexcept
{
#if AN_PLATFORM_SHA_X86
    if (m_implementation == Sha256Implementation::Instructions)
    {
        compressInstructions(m_state, blocks, count);
        return;
    }
#endif
    compressPortable(m_state, blocks, count);
}

void Sha256::update(std::span<const std::byte> bytes) noexcept
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    std::size_t size = bytes.size();
    m_length += size;
    if (m_buffered > 0u)
    {
        const std::size_t taken = (std::min)(m_block.size() - m_buffered, size);
        std::memcpy(m_block.data() + m_buffered, data, taken);
        m_buffered += taken;
        data += taken;
        size -= taken;
        if (m_buffered < m_block.size())
        {
            return;
        }
        compress(m_block.data(), 1u);
        m_buffered = 0u;
    }
    const std::size_t whole = size / m_block.size();
    if (whole > 0u)
    {
        compress(data, whole);
        data += whole * m_block.size();
        size -= whole * m_block.size();
    }
    if (size > 0u)
    {
        std::memcpy(m_block.data(), data, size);
        m_buffered = size;
    }
}

std::array<std::uint8_t, 32> Sha256::finish() noexcept
{
    // The message, a one bit, zeros to eight bytes short of a block's end, and the message's length in bits.
    std::array<std::uint8_t, 128> tail{};
    std::memcpy(tail.data(), m_block.data(), m_buffered);
    tail[m_buffered] = 0x80u;
    const std::size_t blocks = m_buffered + 1u + 8u <= m_block.size() ? 1u : 2u;
    const std::uint64_t bits = m_length * 8u;
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        tail[blocks * m_block.size() - 1u - index] = static_cast<std::uint8_t>(bits >> (8u * index));
    }
    compress(tail.data(), blocks);
    std::array<std::uint8_t, 32> digest{};
    for (std::size_t word = 0u; word < m_state.size(); ++word)
    {
        for (std::size_t index = 0u; index < 4u; ++index)
        {
            digest[4u * word + index] = static_cast<std::uint8_t>(m_state[word] >> (24u - 8u * index));
        }
    }
    restart();
    return digest;
}

std::optional<std::array<std::uint8_t, 32>> sha256OfFile(const std::filesystem::path& path,
                                                         Sha256Implementation implementation)
{
#if defined(_WIN32)
    // Shared as the product's other readers open a file, so a digest never locks out a reader or a writer.
    std::FILE* opened = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
#else
    std::FILE* opened = std::fopen(path.c_str(), "rb");
#endif
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(opened, &std::fclose);
    if (!file)
    {
        return std::nullopt;
    }
    // A read this large goes to the operating system directly rather than through the stream's own buffer.
    std::vector<std::byte> buffer(std::size_t{1u} << 20u);
    Sha256 hash(implementation);
    for (;;)
    {
        const std::size_t read = std::fread(buffer.data(), 1u, buffer.size(), file.get());
        hash.update(std::span<const std::byte>(buffer.data(), read));
        if (read < buffer.size())
        {
            break;
        }
    }
    if (std::ferror(file.get()) != 0)
    {
        return std::nullopt;
    }
    return hash.finish();
}

std::string sha256Hex(const std::array<std::uint8_t, 32>& digest)
{
    std::string text;
    text.reserve(64u);
    for (const std::uint8_t value : digest)
    {
        for (const std::uint8_t nibble :
             {static_cast<std::uint8_t>(value >> 4u), static_cast<std::uint8_t>(value & 0x0fu)})
        {
            text.push_back(static_cast<char>(nibble < 10u ? '0' + nibble : 'a' + nibble - 10u));
        }
    }
    return text;
}

} // namespace an::platform
