// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A borrowed frame reader and its smallest readable unit.
///
/// `StackFrameSource` is the one seam through which `executeStack` reads frames: the caller
/// implements it over whatever holds the frames - decoded images, a file per frame, a sequence -
/// and the executor asks it for bands of rows as the plan needs them. `StackSourceAccess` is the
/// one fact about a source the planner needs: whether a band costs a whole frame's decode or only
/// its own bytes. The source hands back bands as stored; bringing them to the tile width, through
/// calibration and normalization, is the source path's work, not the source's.
///
/// `ResidentStackFrameSource` in `Algorithms/Stacking/StackExecutor.h` is the implementation for
/// frames that are already resident.
#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{

class ExecutionContext;

/// How the source of a stack can read one frame.
///
/// The one fact about a source the planner needs and the descriptor does not carry. It decides
/// whether a tile of every frame is read once or every frame is re-read in full for every tile.
enum class StackSourceAccess : std::uint8_t
{
    /// A frame is decoded whole; a tile of it is a window onto that whole frame.
    WholeFrame,

    /// A rectangle of a frame can be read on its own, so a tile costs a tile's bytes.
    Region,

    /// As `Region`, and reads of different frames may run at the same time: the executor may hold
    /// up to the request's `concurrentFrameReads` of them in flight at once, each on one of the
    /// operation's workers. A source that declares it makes `read` safe to call concurrently for
    /// different frames; two reads of one frame are never in flight together.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    ConcurrentRegion
};

/// Whether a value names an access this build defines.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param access the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStackSourceAccess(StackSourceAccess access) noexcept
{
    return static_cast<std::uint8_t>(access) <= static_cast<std::uint8_t>(StackSourceAccess::ConcurrentRegion);
}

/// Whether an access reads a band of a frame's rows on its own rather than the whole frame.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param access the value to test.
/// @return true for `StackSourceAccess::Region` and `StackSourceAccess::ConcurrentRegion`.
[[nodiscard]] constexpr bool isStackSourceRegionAccess(StackSourceAccess access) noexcept
{
    return access == StackSourceAccess::Region || access == StackSourceAccess::ConcurrentRegion;
}

/// The access's own name.
///
/// Thread-safe and allocation-free.
///
/// @param access the access to name. A value outside the enumeration is named
///        `"UnknownStackSourceAccess"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stackSourceAccessName(StackSourceAccess access) noexcept;

/// A borrowed stored band, defined in `Algorithms/Stacking/StackSource.h`.
struct StackSourceBand;

/// Where a stack's frames come from, one band at a time.
///
/// The executor asks for the rows it needs and the source produces them as stored: under
/// `StackSourceAccess::WholeFrame` every request is the whole frame and the executor cuts the tile's
/// rows from it, and under `StackSourceAccess::Region` a request is a band of rows the source reads
/// on its own. The executor lends every read a buffer the source may decode into - a slot of the
/// plan's read buffer, or, under region access when nothing is to be done to the samples, the
/// frame's own band of the tile. A band the source returns inside that buffer is the executor's to
/// transform in place, and a band anywhere else is borrowed - a frame the source keeps resident -
/// which the executor reads and never writes.
///
/// A borrowed band, its layout and its mask stay valid until the next `read` of the same frame or
/// the source's destruction, whichever is first. Reads are sequential - the executor never holds two
/// reads of one source in flight, so a source needs no locking of its own - unless the source
/// declares `StackSourceAccess::ConcurrentRegion`, which lets reads of different frames overlap.
class StackFrameSource
{
public:
    /// A source is destroyed through this interface by whoever built it.
    virtual ~StackFrameSource() = default;

    /// How this source reads a frame. Fixed for the source's life, because the plan is made from it.
    ///
    /// @return whole frames or regions.
    [[nodiscard]] virtual StackSourceAccess access() const noexcept = 0;

    /// Produces the stated rows of one frame, as stored.
    ///
    /// The execution is the one the stack is running as, handed over on every read rather than
    /// captured when the source was built, so a source outlives no execution and reads for the
    /// operation that is actually asking. A source that decodes checks cancellation itself; the
    /// executor checks it before and after every read. An exception that escapes `read` is caught
    /// at the executor's boundary and becomes this read's failure.
    ///
    /// @param execution the running operation: cancellation, deadline and diagnostics.
    /// @param frameIndex which frame, below the model's frame count.
    /// @param firstRow the frame row the band begins at; zero under whole-frame access.
    /// @param rows how many rows, at least one; the frame's height under whole-frame access.
    /// @param buffer the buffer the executor lends this read, at least the frame's stored bytes
    ///        under whole-frame access and the band's under region access, which the source may
    ///        decode into.
    /// @return the band - `frameIndex` and `firstRow` as asked, the stack's stored representation,
    ///         a layout of exactly `rows` rows at the frame's width and channel count, the frame's
    ///         rule and a mask exactly when the rule needs one - or the failure the executor returns
    ///         as the stack's, with this read as its cause. The executor refuses a band that breaks
    ///         any of these.
    [[nodiscard]] virtual Result<StackSourceBand> read(const ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) = 0;

protected:
    /// A source is built by what derives from it.
    StackFrameSource() = default;

    /// Copying the interface copies nothing; what derives decides.
    StackFrameSource(const StackFrameSource&) = default;

    /// Assigning the interface assigns nothing; what derives decides.
    ///
    /// @return this source.
    StackFrameSource& operator=(const StackFrameSource&) = default;
};

} // namespace an
