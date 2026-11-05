// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Values shared by the authoritative scalar kernel families.
///
/// These records are deliberately smaller than an operation plan. They borrow one invocation's
/// validity inputs, name the resident image component a family reads, report its selection and
/// writes, and name the coordinate convention shared by resample and gather/scatter. Family
/// parameters and entry points live in their own headers.

#pragma once

#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <cstdint>
#include <optional>

namespace an
{

/// One operation's policy and permission to run. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// A validated geometry and its strides. Defined in `Core/ImageLayout.h`.
///
/// Named here rather than included, because the record below only borrows a pointer to one. A
/// caller that reaches a family entry point holds a layout already, and its family header includes
/// the definition.
class ImageLayout;

/// The validity inputs borrowed by one scalar kernel invocation.
///
/// The descriptor is always present. A packed mask is present exactly when the descriptor says
/// `ValidityRule::ExplicitMask`; it must cover the input plane, and `maskPlane` selects which of a
/// shared or per-channel mask's bit-planes is read. Every pointer is borrowed for the call only.
struct KernelValidityView
{
    /// The rule used to decide whether an input sample participates.
    ValidityDescriptor descriptor{};

    /// The packed bits, required only for `ValidityRule::ExplicitMask` and otherwise null.
    const ValidityMaskView<const std::uint8_t>* mask = nullptr;

    /// Which mask bit-plane to read. Zero for a shared mask.
    std::uint32_t maskPlane = 0u;
};

/// One stored image component selected as a kernel's input.
///
/// The samples and layout are borrowed for the call. `plane` and `sampleInPixel` together select a
/// channel from either planar, interleaved, or mixed storage without repacking it, which is what
/// lets a family read an image as it is resident rather than after a copy. It carries no extent of
/// its own: the layout has one, and a second copy could disagree with it.
struct KernelPlaneSource
{
    /// The resident representation the layout's element width must describe.
    SampleType sampleType = SampleType::Float32;

    /// First byte of the image storage, borrowed for the call and never null.
    const void* samples = nullptr;

    /// The validated geometry and strides of `samples`, borrowed for the call and never null.
    const ImageLayout* layout = nullptr;

    /// Which stored plane to read, below `layout->planeCount()`.
    std::uint32_t plane = 0u;

    /// Which component of each pixel in that plane, below `layout->samplesPerPixel()`.
    std::uint32_t sampleInPixel = 0u;
};

/// A closed interval of sample values that narrows a selection beyond its validity rule.
///
/// A clipped estimator needs "the samples this image says mean something, and also within these
/// bounds", and the two halves of that sentence cannot be one `ValidityDescriptor`: a stated range
/// replaces the image's own rule rather than composing with it, so an image whose samples are
/// selected by a packed mask would lose the mask the moment a clip window was expressed that way.
/// The window therefore travels beside the validity view, and both are applied.
///
/// Both endpoints are included. They must be finite and ordered, and a window whose lowest end is
/// above its highest is refused rather than read as empty. Two equal endpoints select the samples
/// holding exactly that value.
struct KernelValueWindow
{
    /// Smallest value a sample may hold and still participate.
    double lowest = 0.0;

    /// Largest value a sample may hold and still participate.
    double highest = 0.0;

    /// Two windows agree when both endpoints agree exactly.
    friend constexpr bool operator==(const KernelValueWindow& left, const KernelValueWindow& right) noexcept = default;
};

/// Which part of a source plane one kernel reads, and which of its values participate.
///
/// The rectangle's right and bottom edges are exclusive and the whole of it must lie within the
/// source layout; either dimension may be zero, which is an empty selection rather than a failure
/// to describe one. Coordinates are relative to the source the kernel is given, and an explicit
/// packed mask continues to describe the complete source image, so a rectangle whose horizontal
/// origin is not a multiple of eight needs no temporary mask of its own.
///
/// The samples are read where they lie. A rectangle keeps the source's row and plane strides and
/// nothing is compacted, so selecting part of an image costs no copy of it.
struct KernelPlaneSelection
{
    /// First selected column, counted from the source's left edge.
    std::uint32_t originX = 0u;

    /// First selected row, counted from the source's top edge.
    std::uint32_t originY = 0u;

    /// Number of selected columns. Zero makes the selection empty.
    std::uint32_t width = 0u;

    /// Number of selected rows. Zero makes the selection empty.
    std::uint32_t height = 0u;

    /// The value interval that further narrows the selection, or nothing to accept every value the
    /// validity rule admits.
    std::optional<KernelValueWindow> valueWindow{};
};

/// Counts common to every scalar kernel result.
struct KernelRunResult
{
    /// How many logical inputs were considered and how many passed validity.
    ValidityCount selection{};

    /// How many destination writes were performed. Invalid inputs leave their destination slots
    /// untouched and therefore do not increment this count.
    std::uint64_t outputsWritten = 0u;

    /// Two run results agree when both the validity count and write count agree.
    friend constexpr bool operator==(const KernelRunResult& left, const KernelRunResult& right) noexcept = default;
};

/// One location in an image under the pixel-center convention.
struct KernelCoordinate
{
    /// Horizontal coordinate; integers name pixel centers.
    double x = 0.0;

    /// Vertical coordinate; integers name pixel centers.
    double y = 0.0;

    /// Coordinates agree when both components agree exactly.
    friend constexpr bool operator==(const KernelCoordinate& left, const KernelCoordinate& right) noexcept = default;
};

} // namespace an
