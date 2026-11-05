// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The reasons a checked geometry refuses its arguments.
///
/// This is a vocabulary rather than a type with behaviour, and it is its own header because it
/// belongs to neither of the two types that produce it and to more than the two layers that consume
/// it: the extent and the layout both refuse for these reasons, and storage, allocation and the
/// codecs all have to say which one happened.
///
/// The distinctions are the ones a caller acts on differently. Over a configured maximum is a
/// setting somebody can raise; arithmetic that overflowed is a geometry no setting will admit; a
/// stride below its minimum is a descriptor to correct. Collapsing them into one "invalid geometry"
/// would leave every caller re-deriving which of those it was.

#pragma once

#include <AfterNightEngine/Core/Api.h>

namespace an
{

/// Why a checked extent or layout refused the arguments it was given.
///
/// Returned by the `check` entry point of each geometry type, which is where a caller goes for the
/// reason; the matching `create` entry point returns the value and no reason. `None` is the only
/// enumerator that means the arguments were accepted.
///
/// An extent checks facts before policy: a zero, then arithmetic that does not fit, and only then
/// the configured maxima, so raising a limit never turns one of its refusals into a different one -
/// a geometry refused for a reason above the policy line is one no configuration admits. A layout
/// checks its own zeros, alignment, arrangement and strides, then a size this platform cannot
/// address, before re-checking the extent's configured maxima and finally the maximum image size.
/// A malformed layout is therefore refused for the same intrinsic reason under different limits.
enum class GeometryError
{
    /// The arguments describe a geometry this engine can represent and is configured to admit.
    None,

    /// A width, height, channel count, plane count, samples-per-pixel or element size was zero.
    /// There is no image with no rows, no columns, no channels or no bytes per sample, and a zero
    /// here is nearly always an unset field rather than an intended value.
    ZeroDimension,

    /// A width or height is larger than the configured maximum. The default maximum is far above
    /// any real acquisition and far below what a damaged header produces.
    DimensionTooLarge,

    /// The channel count is larger than the configured maximum.
    ChannelCountTooLarge,

    /// The plane count and the samples each plane holds for one pixel do not account for exactly
    /// the channels the extent declares. One plane per channel and one plane holding every channel
    /// are both valid; a pair that leaves channels unstored or stores more than exist is not.
    PlaneCountMismatch,

    /// The requested alignment is not a power of two, so nothing can satisfy it and the arithmetic
    /// that would round to it is undefined.
    AlignmentNotPowerOfTwo,

    /// The element size is not a power of two. An element is one scalar sample, and the stride
    /// rules exist to keep a typed pointer to one valid, which only means anything for a type
    /// whose alignment divides its size. Allowing an odd size would also let a derived stride
    /// satisfy the requested alignment and not the element size at the same time, so the engine
    /// would refuse a layout it had just computed itself.
    ElementSizeNotPowerOfTwo,

    /// A row or plane stride is smaller than the samples it has to hold. This is the descriptor
    /// that reads the next row's first sample instead of its own last one.
    StrideTooSmall,

    /// A row or plane stride is not a whole number of elements, or does not honour the requested
    /// alignment. A stride that is not a multiple of the element size puts every row after the
    /// first at an address no typed pointer can reach.
    StrideMisaligned,

    /// A size computation overflowed 64 bits. This is about representability rather than policy,
    /// so no configured maximum admits it.
    ArithmeticOverflow,

    /// The total byte count is representable in the arithmetic and not on this platform: larger
    /// than the allocator can be asked for, or than a pointer difference can span.
    SizeNotRepresentable,

    /// The total byte count is larger than the configured maximum for a single image.
    ImageTooLarge,

    /// The layout describes samples of a different size than the type reading them. A layout is
    /// untyped by design - it holds a byte count per element - so this is the rule that pairs one
    /// with a C++ type, and it is the difference between a run over 16-bit samples and the same run
    /// reading half as many 32-bit ones out of the same bytes.
    ///
    /// This and the three below it sit after `ImageTooLarge` rather than beside the reasons they
    /// belong with, because they arrived with typed storage and views and every enumerator above
    /// them was already crossing the library boundary as a value. Reordering the enumeration to
    /// read better would have renumbered those, which is a change no compiler and no test reports.
    ElementSizeMismatch,

    /// The buffer is smaller than the geometry written through it. This is the reason a view over
    /// memory the engine did not allocate refuses: the strides and the extent describe more bytes
    /// than the caller handed over, and the samples past the end are somebody else's.
    BufferTooSmall,

    /// The buffer does not begin on a boundary a sample of this size may be read from. A typed
    /// pointer into a misaligned buffer is undefined behaviour on every platform and a fault on
    /// some of them, so it is refused where the buffer is adopted rather than where it is read.
    BaseAddressMisaligned,

    /// The requested region is not wholly inside the image it is taken from. A subview one column
    /// past the right edge is a loop that reads the next row's first sample, which is the defect
    /// this whole vocabulary exists to make impossible to reach.
    RegionOutOfBounds
};

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// The spelling of the enumerator: `GeometryError::StrideTooSmall` is `"StrideTooSmall"`. It is a
/// name rather than a sentence, because the sentence belongs to whatever is refusing and the name
/// is what identifies the rule across a log, a test failure and a returned `Status`. Code still
/// branches on the enumerator; nothing parses this.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownGeometryError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* geometryErrorName(GeometryError error) noexcept;

} // namespace an
