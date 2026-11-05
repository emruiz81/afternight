// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the engine's persistent memory comes from, and what it costs.
///
/// An allocation that reports a refusal as a null nobody checks, rounds its alignment up after the
/// fact and counts nothing leaves three separate problems, and this interface is the answer to all
/// three.
///
/// **A refusal is a returned failure with the numbers attached.** "The allocator said no" and "this
/// does not fit the budget you configured" are different answers a caller acts on differently -
/// tiling and streaming are answers to the second and not to the first - so they are different
/// codes, and both carry what was asked for beside what was available.
///
/// **Alignment is asked for and delivered.** A buffer that a vector kernel will read has to begin on
/// a boundary the kernel can use, and rounding a pointer up after the fact wastes the padding and
/// loses the ability to free it. It is a parameter of the request here, and the alignment given back
/// is the alignment that was asked for.
///
/// **What is outstanding is counted, and what was ever outstanding is remembered.** A memory budget
/// is not enforceable without the first, and "how much did that stack peak at" has no answer without
/// the second. The counters are part of the interface rather than a debugging extra, because a
/// benchmark that reports peak bytes needs them in a release build.
///
/// The interface is deliberately not an allocator in the standard-library sense. Nothing in this
/// engine is a container templated on it, so the allocator type never reaches a public type's
/// signature, never affects one's layout, and can be replaced without anything above it being
/// recompiled - which is what "no allocator leakage" costs and buys.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>

namespace an
{

/// What a memory resource has done, for a budget, a benchmark or a diagnostic.
///
/// A plain value read out under one lock or one set of atomics, so the fields are consistent with
/// each other rather than sampled one at a time. Every member carries a default, so a caller
/// building one - in a test double, in a report - names only what it is setting.
struct MemoryCounters
{
    /// How many bytes are outstanding right now, counting only what was asked for rather than what
    /// the platform actually reserved.
    std::uint64_t liveBytes = 0u;

    /// How many allocations are outstanding right now.
    std::uint64_t liveAllocations = 0u;

    /// The largest `liveBytes` ever reached. Never falls, which is what makes it the number a
    /// benchmark reports for a run rather than for a moment.
    std::uint64_t peakBytes = 0u;

    /// How many bytes have ever been handed out, counting each allocation once.
    std::uint64_t totalBytes = 0u;

    /// How many allocations have ever been handed out.
    std::uint64_t totalAllocations = 0u;

    /// How many requests were refused, whether by the budget or by the platform. A run whose
    /// timings are surprising and whose denied count is not zero is a run that spent its time
    /// recovering.
    std::uint64_t deniedAllocations = 0u;
};

/// Where the engine's persistent memory comes from.
///
/// Implemented by the engine and owned by an engine context; a caller may also implement one and
/// name it in `EngineContextOptions::memoryResource`, which is how a test refuses an allocation at a
/// chosen boundary without exhausting the machine. A context built over one still owns its own
/// resource, still enforces its budget and still reports its own counters: only the source of the
/// admitted bytes changes, and a refusal from the supplied resource reaches the caller as it was
/// given.
///
/// Every implementation must be safe to call from several threads at once: one resource serves a
/// whole engine context, and the operations in one context run concurrently by design.
class AFTERNIGHTENGINE_INTERFACE MemoryResource
{
public:
    /// Virtual, so that a resource held by base pointer is destroyed as what it is.
    virtual ~MemoryResource() = default;

    /// Takes memory, or reports why it cannot.
    ///
    /// @param bytes how many bytes are needed. Zero is refused with `ErrorCode::InvalidArgument`:
    ///        an allocation nobody may dereference is a mistake at the call site rather than a
    ///        cheap request.
    /// @param alignment the boundary the returned address must sit on. Must be a power of two, or
    ///        the request is refused with `ErrorCode::InvalidArgument`.
    /// @return the address, which is aligned as asked and whose contents are unspecified; or
    ///         `ErrorCode::MemoryBudgetExceeded` when the engine's configured budget refuses it,
    ///         carrying the request, what was available and the limit as typed context, or
    ///         `ErrorCode::AllocationFailed` when the platform refuses it. The engine's own
    ///         resource, built over a caller's resource, returns that resource's refusal unchanged.
    [[nodiscard]] virtual Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) = 0;

    /// Takes memory whose every requested byte is zero, or reports why it cannot.
    ///
    /// An implementation may obtain pages the platform has already zeroed, which is cheaper than
    /// writing a whole frame to establish a value the platform guarantees. The default calls
    /// `allocate` and clears what it returns, so a caller-supplied resource that implements only
    /// `allocate` still serves this. Accounting, ownership and thread safety are those of
    /// `allocate`, and the zeroing is not charged as a second allocation.
    ///
    /// @param bytes how many bytes are needed. Zero is refused with `ErrorCode::InvalidArgument`.
    /// @param alignment the boundary the returned address must sit on. Must be a power of two, or
    ///        the request is refused with `ErrorCode::InvalidArgument`.
    /// @return the zero-filled address, or the same refusals `allocate` reports.
    [[nodiscard]] virtual AFTERNIGHTENGINE_API Result<void*> allocateZeroed(std::uint64_t bytes,
                                                                            std::uint64_t alignment);

    /// Gives memory back.
    ///
    /// The size and alignment must be the ones `allocate` or `allocateZeroed` was given: the caller
    /// states them so that the accounting needs no record of its own, and how a resource keeps its
    /// underlying platform allocation is its own business.
    ///
    /// @param pointer what `allocate` or `allocateZeroed` returned. A null pointer is a no-op, so a
    ///        cleanup path that runs after a refused allocation needs no branch of its own.
    /// @param bytes what was asked for.
    /// @param alignment what was asked for.
    virtual void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept = 0;

    /// What this resource has done.
    ///
    /// Thread-safe. The values are consistent with each other and may be out of date the moment
    /// they are read, which is the same caveat every count of concurrent work carries.
    ///
    /// @return the counters.
    [[nodiscard]] virtual MemoryCounters counters() const noexcept = 0;

protected:
    /// Constructible only by a derived class: this is an interface, not a value.
    MemoryResource() = default;

    /// Protected rather than public, so that a resource is never copied through a base pointer and
    /// sliced down to its interface. A derived resource that wants to be copyable still can be.
    ///
    /// @param other the resource to copy from.
    MemoryResource(const MemoryResource& other) = default;

    /// Protected for the same reason as the copy constructor.
    ///
    /// @param other the resource to copy from.
    /// @return this resource.
    MemoryResource& operator=(const MemoryResource& other) = default;
};

} // namespace an
