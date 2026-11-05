// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Holds the blocks this executable frees, poisoned, while a case watches for work that reaches an object it destroyed.
///
/// A completion that runs after its view is gone reads a freed block, and a freed block usually still holds what the
/// object held, so the completion does nothing visible and the case passes whether or not the completion was guarded.
/// While a quarantine is alive, every block this executable's `operator delete` releases is filled with a poison byte
/// and kept rather than freed: a pointer read from it is not an address, so following one faults, and no allocation
/// made during the case reuses the block. The suite replaces the global allocation functions for that reason.
///
/// Under the address sanitizer a held block is poisoned through the sanitizer rather than filled - it marks an array's
/// cookie and reports a write to it - so the first read of a held block is reported by name. Under the thread sanitizer
/// the quarantine replaces nothing and holds nothing: that runtime defines the allocation functions itself, strongly,
/// and the link refuses a second definition.

#pragma once

#include <cstddef>

namespace an::tests::viewer
{

/// The byte a quarantined block is filled with; eight of them are not a canonical address.
inline constexpr unsigned char kFreedMemoryPoison = 0xA5u;

// 1 where the quarantine replaces the allocation functions, 0 under the thread sanitizer, whose runtime defines them.
#if defined(__SANITIZE_THREAD__)
#define AN_FREED_MEMORY_QUARANTINE_HOLDS 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define AN_FREED_MEMORY_QUARANTINE_HOLDS 0
#endif
#endif
#if !defined(AN_FREED_MEMORY_QUARANTINE_HOLDS)
#define AN_FREED_MEMORY_QUARANTINE_HOLDS 1
#endif

/// Whether the quarantine replaces the allocation functions and holds what is freed: everywhere but under the thread
/// sanitizer.
inline constexpr bool kQuarantineHoldsFreedBlocks = AN_FREED_MEMORY_QUARANTINE_HOLDS != 0;

/// While alive, holds every block this executable frees, poisoned, and frees them all when it ends.
///
/// One may be alive at a time. A block released while the quarantine is full is freed as usual.
class FreedMemoryQuarantine
{
public:
    FreedMemoryQuarantine();
    ~FreedMemoryQuarantine();

    FreedMemoryQuarantine(const FreedMemoryQuarantine&) = delete;
    FreedMemoryQuarantine& operator=(const FreedMemoryQuarantine&) = delete;

    /// How many blocks are held; none where `kQuarantineHoldsFreedBlocks` is false.
    [[nodiscard]] std::size_t held() const noexcept;
};

} // namespace an::tests::viewer
