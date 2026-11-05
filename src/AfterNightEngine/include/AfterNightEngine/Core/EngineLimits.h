// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The maxima the engine is configured with, and the budget it works within.
///
/// Two different things live here and they are deliberately not merged. A maximum dimension is a
/// filter against nonsense: it exists so that a header claiming two billion columns is refused
/// before anything is allocated, and the default is set well above any real acquisition so that
/// hitting it means something is wrong rather than something is large. A memory budget is an
/// operating constraint: it says how much the engine may have outstanding at once, so that a request
/// which would exceed it fails as a budget failure a caller can answer by tiling or streaming,
/// rather than as an allocator refusal it can only report.
///
/// Neither is hard-coded. A constant maximum is a number that is either too small for a legitimate
/// mosaic or too large to catch a corrupt header, and usually both; these are defaults a caller
/// changes deliberately. What is never configurable is representability - a geometry whose size does
/// not fit the arithmetic or the platform is refused whatever these say.

#pragma once

#include <AfterNightEngine/Core/CheckedSize.h>

#include <cstdint>
#include <limits>

namespace an
{

/// The default maximum image width, in pixels.
///
/// A million columns is roughly two orders of magnitude beyond the largest sensor and mosaic this
/// engine is built for, and two orders of magnitude below the values a damaged or misread header
/// produces, which is the gap the default is chosen to sit in.
inline constexpr std::uint32_t kDefaultMaxImageWidth = 1000000u;

/// The default maximum image height, in pixels. See `kDefaultMaxImageWidth`.
inline constexpr std::uint32_t kDefaultMaxImageHeight = 1000000u;

/// The default maximum number of channels.
///
/// Ordinary astronomical work needs one or three, and a spectral cube carried as channels reaches
/// the hundreds or low thousands; four thousand is above all of those and below anything a wrong
/// field yields.
inline constexpr std::uint32_t kDefaultMaxChannelCount = 4096u;

/// The default maximum size of a single image, in bytes.
///
/// Sixty-four gibibytes is larger than any single frame this engine expects to hold resident and
/// small enough that a geometry reaching it is worth refusing rather than attempting.
inline constexpr std::uint64_t kDefaultMaxImageBytes = 64ull * 1024ull * 1024ull * 1024ull;

/// The default memory budget, which refuses nothing.
///
/// A default budget that refused anything would be an invisible cap set by whoever wrote this line
/// rather than by the machine the engine runs on, and every host that outgrew it would meet a
/// failure nobody configured. Until a host sets a real number - from the memory it measured, or
/// from what it wants to leave for everything else - the physical allocator is the backstop.
inline constexpr std::uint64_t kDefaultMemoryBudgetBytes = (std::numeric_limits<std::uint64_t>::max)();

/// The configured maxima and memory budget a geometry is checked against.
///
/// A plain value: it is copied into whatever needs it, and a change takes effect only in an engine
/// context built with it, because a live context's limits are fixed at construction. Every field
/// can be raised or lowered; none of them can admit a geometry whose size is not representable.
struct EngineLimits
{
    /// Largest admissible image width, in pixels.
    std::uint32_t maxImageWidth;

    /// Largest admissible image height, in pixels.
    std::uint32_t maxImageHeight;

    /// Largest admissible number of channels.
    std::uint32_t maxChannelCount;

    /// Largest admissible size for one image, in bytes, counting the padding its strides describe.
    std::uint64_t maxImageBytes;

    /// How many bytes the engine may have outstanding at once. This is an aggregate budget rather
    /// than a maximum for a single allocation: what it bounds is the total of everything currently
    /// held, which is why the accounting that uses it has to pass in what is already outstanding.
    std::uint64_t memoryBudgetBytes;
};

/// The limits an engine starts with when a caller states none.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @return the defaults declared by the constants in this header.
[[nodiscard]] constexpr EngineLimits defaultEngineLimits() noexcept
{
    return EngineLimits{.maxImageWidth = kDefaultMaxImageWidth,
                        .maxImageHeight = kDefaultMaxImageHeight,
                        .maxChannelCount = kDefaultMaxChannelCount,
                        .maxImageBytes = kDefaultMaxImageBytes,
                        .memoryBudgetBytes = kDefaultMemoryBudgetBytes};
}

/// Whether one more allocation fits the configured budget.
///
/// The budget is aggregate, so the answer depends on what is already held: the caller that tracks
/// outstanding bytes passes them in, and this decides. A total that would overflow is refused
/// rather than wrapped, which is the case that would otherwise turn an impossible request into an
/// admitted one.
///
/// Thread-safe, allocation-free, and usable in a constant expression. It reads the numbers it is
/// given and keeps no state of its own; the tracking of outstanding bytes belongs to whatever owns
/// the allocations.
///
/// @param limits the configured limits, whose `memoryBudgetBytes` is the bound.
/// @param outstandingBytes bytes already accounted against the budget.
/// @param requestedBytes bytes the caller is about to ask for.
/// @return true when the two together are within the budget; false when they exceed it or their
///         sum is not representable.
[[nodiscard]] constexpr bool fitsMemoryBudget(const EngineLimits& limits, std::uint64_t outstandingBytes,
                                              std::uint64_t requestedBytes) noexcept
{
    const CheckedSize total = CheckedSize{outstandingBytes} + CheckedSize{requestedBytes};
    return total.isValid() && total.valueOr(0u) <= limits.memoryBudgetBytes;
}

} // namespace an
