// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - one image-level statistics selection reduced to what a kernel takes.
//
// Every operation on the statistics surface is handed the same three things - a channel of an image
// known only at run time, the rule that says which of its samples mean something, and an optional
// rectangle - and has to turn them into the plane, validity view and rectangle a kernel reads. That
// translation is where a channel could quietly become a plane, where a mask could be accepted that
// does not cover the image, and where a rectangle could be admitted that runs off the end of it.
// Writing it once is what stops two operations on one surface answering those differently.

#pragma once

#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>

/// The statistics operations' shared, private translation from an image-level selection to the
/// kernel substrate's plane, validity view and rectangle.
namespace an::statistics_detail
{

/// One statistics call expressed in the kernel substrate's own vocabulary.
///
/// Every pointer in it is borrowed from the call that produced it - the image storage, its layout
/// and the caller's packed mask - so it must not outlive them.
struct ResolvedSelection
{
    /// The selected channel, addressed through the layout rather than assumed to be a plane.
    KernelPlaneSource source{};

    /// The value rule and the bit-plane of any packed mask that goes with it.
    KernelValidityView validity{};

    /// The rectangle to read, which is the whole image when the caller named none.
    KernelPlaneSelection region{};
};

/// Turns one image-level statistics selection into a checked kernel selection.
///
/// The source pointer, sample type and layout must agree; the channel must exist; a packed mask is
/// required exactly for the rule that names one and must cover the image with either one shared
/// plane or one plane per channel; and the rectangle must be contained by the image. An empty
/// rectangle is described rather than refused, because reporting an empty selection with its counts
/// belongs to the operation rather than to this translation.
///
/// @param sampleType the resident representation the layout's element width must describe.
/// @param samples first byte of the image storage, borrowed for the call.
/// @param layout the validated geometry and strides, borrowed for the call.
/// @param channelIndex which channel, below the layout extent's channel count.
/// @param selection the validity rule, optional full-image mask and optional rectangle.
/// @param operation the name carried by any failure.
/// @return the kernel selection, or the first reason the arguments do not describe one.
[[nodiscard]] Result<ResolvedSelection>
resolveStatisticsSelection(SampleType sampleType, const void* samples, const ImageLayout& layout,
                           std::uint32_t channelIndex, const StatisticsSelection& selection, const char* operation);

/// Whether two byte ranges share any byte.
///
/// @param first the first range, which may be empty or null.
/// @param firstBytes its length in bytes.
/// @param second the second range, which may be empty or null.
/// @param secondBytes its length in bytes.
/// @return true when both are non-empty and they intersect.
[[nodiscard]] bool rangesOverlap(const void* first, std::uint64_t firstBytes, const void* second,
                                 std::uint64_t secondBytes) noexcept;

} // namespace an::statistics_detail
