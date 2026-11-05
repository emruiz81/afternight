// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - SHA-256 over benchmark payloads.
//
// Two things need a digest and both are load-bearing:
//
//   - output digests, because correctness is compared over the complete output rather than a
//     handful of sampled pixels; and
//   - corpus digests, because the real corpus is host-local and a moved or re-exported file must
//     be detected instead of silently substituted.
//
// SHA-256 is implemented here rather than pulled from a dependency: this library is deliberately
// dependency-light, the volume hashed is bounded by image size, and an archived baseline bundle
// must stay verifiable without whatever crypto library happened to be installed at the time.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// Which block compression a digest uses. Both produce the same digest.
enum class Sha256Implementation
{
    /// The processor's SHA extensions where this process can use them, the portable code otherwise.
    Automatic,

    /// The portable code, which is the reference the extensions are tested against.
    Portable
};

/// Streaming SHA-256. Feed with `update`, finish once with `hex`.
///
/// A runner hashes every output and verifies every corpus payload it loads, hundreds of megabytes
/// per capture row, so where the processor has SHA extensions the whole blocks go through them.
class Sha256
{
public:
    /// @param implementation which block compression to use.
    explicit Sha256(Sha256Implementation implementation = Sha256Implementation::Automatic);

    /// Whether this process can compress with the processor's SHA extensions. Detected once.
    [[nodiscard]] static bool processorExtensionsAvailable() noexcept;

    /// Append `size` bytes. May be called any number of times; chunk boundaries do not affect
    /// the result, which is what lets a runner hash a strided image row by row.
    void update(const void* data, std::size_t size);

    /// Finalize and return the lowercase 64-character hex digest.
    ///
    /// Finalizing more than once is a programmer error; the object is single-shot and further
    /// `update` calls after `hex` have undefined results.
    [[nodiscard]] std::string hex();

    /// One-shot convenience for a contiguous buffer.
    [[nodiscard]] static std::string of(const void* data, std::size_t size);

    /// One-shot convenience for text, hashing its bytes without a trailing null.
    [[nodiscard]] static std::string of(const std::string& text);

private:
    void compress(const std::uint8_t* block);
    void compressBlocks(const std::uint8_t* blocks, std::size_t count);

    std::uint32_t m_state[8];
    std::uint64_t m_bitCount;
    std::uint8_t m_buffer[64];
    std::size_t m_bufferSize;
    bool m_processorExtensions;
};

/// One digest over a set of fixture digests, so a scenario fed several frames records one input
/// identity that every run computes the same way.
///
/// @param digests the fixtures' digests, in frame order.
/// @return the hex digest of the set.
[[nodiscard]] std::string digestFixtureSet(const std::vector<std::string>& digests);

} // namespace an::bench
