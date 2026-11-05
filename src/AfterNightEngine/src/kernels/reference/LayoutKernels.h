// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar kernels a layout move dispatches to, and the shapes they have.
//
// `kernels/reference/ConversionKernels.h` next door owns the three shapes that change what a sample
// *is*. These are the four that change only where it *sits*: a run copied, a run filled, an
// interleaved run split into its channels, and channels joined back into one. Nothing here reads a
// sample's meaning, which is why every one of them is described by an element width rather than by a
// representation - a copy of four bytes is the same copy whether those bytes are a `Float32` or an
// `Int32`, and a kernel that knew the difference would be a kernel with a reason to canonicalise a
// signalling pattern it was only supposed to move.
//
// **The signatures are untyped for the same reason the conversion ones are.** A dispatch table holds
// function pointers, and the representation is resolved once at lookup rather than passed to the
// kernel. The lookup still takes a `SampleType`, because that is what every caller holds; the table
// behind it collapses the eight representations onto the four widths, which is where the object size
// this engine spends on per-instruction-set compilation actually goes.
//
// **These report nothing.** A layout move cannot meet a value it has to refuse - it never looks at
// one - so unlike a conversion there is no outcome to return and no counter to accumulate. Every
// argument is checked by the operation above before the run starts.

#pragma once

#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

namespace an
{

/// The widest interleaved pixel one channel-joining or channel-splitting kernel accepts.
///
/// Two, three and four are the pixel shapes an image in this engine actually has - grey with an
/// alpha, colour, colour with an alpha - and they are also the widths a vector instruction set
/// provides a single load or store for. A wider pixel is moved one channel at a time through the
/// strided copy in `kernels/reference/ConversionKernels.h`, which has no such limit; the operation
/// above chooses between them.
inline constexpr std::uint32_t kMaxInterleavedChannels = 4u;

/// Copies one run of samples without changing what they are or where they sit relative to each
/// other.
///
/// @param source where the samples to copy start.
/// @param destination where the copies go. Never overlaps `source`.
/// @param count how many samples.
using CopyRunFn = void (*)(const void* source, void* destination, std::uint64_t count) noexcept;

/// Writes one sample value over a whole run.
///
/// @param value the one sample to repeat, in the run's own representation. Read once per call as
///        far as the contract is concerned; a kernel may broadcast it.
/// @param destination where the run starts. Never overlaps `value`.
/// @param count how many samples.
using FillRunFn = void (*)(const void* value, void* destination, std::uint64_t count) noexcept;

/// Splits one interleaved run into one contiguous run per channel.
///
/// @param source where the interleaved samples start. Holds `channelCount * pixelCount` samples.
/// @param destinations one contiguous run per channel, in channel order. None overlaps `source` or
///        another destination.
/// @param channelCount how many samples one pixel holds. At least two and at most
///        `kMaxInterleavedChannels`.
/// @param pixelCount how many pixels.
using DeinterleaveRunFn = void (*)(const void* source, void* const* destinations, std::uint32_t channelCount,
                                   std::uint64_t pixelCount) noexcept;

/// Joins one contiguous run per channel into one interleaved run.
///
/// @param sources one contiguous run per channel, in channel order. None overlaps `destination` or
///        another source.
/// @param destination where the interleaved samples go. Holds `channelCount * pixelCount` samples.
/// @param channelCount how many samples one pixel holds. At least two and at most
///        `kMaxInterleavedChannels`.
/// @param pixelCount how many pixels.
using InterleaveRunFn = void (*)(const void* const* sources, void* destination, std::uint32_t channelCount,
                                 std::uint64_t pixelCount) noexcept;

/// The reference copy kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free: the table is an immutable constant with static
/// storage duration.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] CopyRunFn referenceCopyRun(SampleType sampleType) noexcept;

/// The reference fill kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] FillRunFn referenceFillRun(SampleType sampleType) noexcept;

/// The reference channel-splitting kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] DeinterleaveRunFn referenceDeinterleaveRun(SampleType sampleType) noexcept;

/// The reference channel-joining kernel for one representation.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param sampleType what the samples are, which is where the element width comes from.
/// @return the kernel, or null when the value is outside the enumeration.
[[nodiscard]] InterleaveRunFn referenceInterleaveRun(SampleType sampleType) noexcept;

} // namespace an
