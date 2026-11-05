// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar kernels a conversion dispatches to, and the shapes they have.
//
// A dispatch table holds function pointers, so the three things a conversion does to a run of
// samples need three signatures that do not mention a C++ sample type. These are those signatures,
// and this header is where they live because both sides need them: the reference implementations
// below are the oracle, and the compiled backend that registers optimised ones has to declare the
// same shapes to be substitutable for them.
//
// **A kernel reports rather than returns a failure.** `Status` allocates when it carries detail, and
// a kernel is called once per row of a frame; building a described failure inside one would put an
// allocation into the inner loop of every conversion that refuses a sample. So a kernel says which
// sample it stopped at and which rule stopped it, and the operation above turns that into a failure
// once.
//
// **The counters are accumulated, never assigned.** A caller walking an image passes the same
// counters to every row and to every thread's chunk, so a kernel adds and does not set.

#pragma once

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// Where a run kernel stopped, and why.
///
/// A plain aggregate with defaults, so a kernel that converted everything returns `{}` and a reader
/// sees that it did.
struct ConversionRunOutcome
{
    /// Which sample of the run the kernel refused at, counting from the run's first. Meaningless
    /// unless `rejected`.
    std::uint64_t rejectedIndex = 0u;

    /// Whether the kernel stopped early. Everything before `rejectedIndex` has been written.
    bool rejected = false;

    /// Which rule refused: the non-finite policy when true, the range policy when false.
    /// Meaningless unless `rejected`.
    bool rejectedForNonFinite = false;
};

/// Converts one run of samples from one representation to another.
///
/// The two buffers are the run's own samples, contiguous, and the pair of representations is fixed
/// when the kernel is looked up rather than passed to it.
///
/// @param source where the samples to convert start.
/// @param destination where the converted samples go. Never overlaps `source`.
/// @param count how many samples.
/// @param policy the five decisions one sample conversion makes.
/// @param counters what the run met, added to whatever they already hold.
/// @return where the kernel stopped, or a default-constructed outcome when it converted everything.
using ConvertRunFn = ConversionRunOutcome (*)(const void* source, void* destination, std::uint64_t count,
                                              const ConversionPolicy& policy, ConversionCounters& counters) noexcept;

/// Copies one run of samples between two strides without changing what they are.
///
/// What a repack is made of: one of the two strides is one sample and the other is the channel count,
/// and which is which decides whether the run is a gather or a scatter.
///
/// @param source where the samples to copy start.
/// @param sourceStrideSamples how many samples from one source sample to the next.
/// @param destination where the copies go. Never overlaps `source`.
/// @param destinationStrideSamples how many samples from one destination sample to the next.
/// @param count how many samples.
using StrideCopyRunFn = void (*)(const void* source, std::uint64_t sourceStrideSamples, void* destination,
                                 std::uint64_t destinationStrideSamples, std::uint64_t count) noexcept;

/// Reverses the bytes of one run of samples, in place.
///
/// The samples are not interpreted; a run of four-byte samples has each group of four bytes
/// reversed, whatever those bytes mean.
///
/// @param samples where the run starts.
/// @param count how many samples.
using ByteSwapRunFn = void (*)(void* samples, std::uint64_t count) noexcept;

/// The reference conversion kernel for one ordered pair of representations.
///
/// Thread-safe, reentrant, and allocation-free: the table is an immutable constant with static
/// storage duration.
///
/// @param source what the samples are.
/// @param destination what they are to become.
/// @return the kernel, or null when either value is outside the enumeration.
[[nodiscard]] ConvertRunFn referenceConvertRun(SampleType source, SampleType destination) noexcept;

/// The reference strided-copy kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] StrideCopyRunFn referenceStrideCopyRun(SampleType sampleType) noexcept;

/// The reference byte-swap kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration or names a one-byte
///         representation, which has nothing to swap.
[[nodiscard]] ByteSwapRunFn referenceByteSwapRun(SampleType sampleType) noexcept;

class ExecutionContext;

/// The conversion kernel an operation runs for one ordered pair of representations: the compiled run of the
/// instruction set its execution selects, or the scalar run where the execution asks for the reference backend.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param execution the operation whose backend and instruction set decide.
/// @param source what the samples are.
/// @param destination what they are to become.
/// @return the kernel, or null when either value is outside the enumeration.
[[nodiscard]] ConvertRunFn convertRunFor(const ExecutionContext& execution, SampleType source,
                                         SampleType destination) noexcept;

} // namespace an
