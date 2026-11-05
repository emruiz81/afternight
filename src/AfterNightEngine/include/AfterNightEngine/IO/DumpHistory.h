// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The history of one region across a list of developer dumps, read into one block of memory.
///
/// An operation across many frames - a per-pixel statistic over a stack, for instance - needs the same
/// region of every frame at once. `DumpHistory::read` reads that region from each dump in turn, never
/// holding more than the history and one decoded region, and lays the samples out either frame by
/// frame or in blocks that keep each pixel's history close together. Samples keep their stored
/// representation and byte order; nothing converts them.

#pragma once

#include <AfterNightEngine/IO/DumpReader.h>

#include <span>
#include <string_view>

namespace an
{
/// How one region's samples are arranged across its frame history.
enum class DumpHistoryLayout : std::uint8_t
{
    /// Resolves to `FrameMajor`, the reference arrangement; blocked storage is used only when asked for
    /// by name.
    Automatic,
    FrameMajor, ///< Each frame's samples are contiguous, in canonical planar order.
    /// The samples are grouped in blocks, and each block holds every frame's copy of it, in frame order.
    BlockedPixelMajor
};

/// Why a history has the arrangement it has; it implies nothing about how the samples are used.
enum class DumpHistoryLayoutReason : std::uint8_t
{
    AutomaticReference, ///< `Automatic` was requested and resolved to the reference arrangement.
    ForcedFrameMajor, ///< The caller requested `FrameMajor`.
    ForcedBlocked ///< The caller requested `BlockedPixelMajor`.
};

/// How dump files are opened while a history is read.
enum class DumpHistoryReaderPolicy : std::uint8_t
{
    Reopen, ///< Each dump is opened, validated and closed for this read alone.
    Pooled ///< Readers come from the bounded pool the caller supplies.
};

/// The memory and file-handle choices for reading a history.
struct DumpHistoryOptions
{
    DumpHistoryLayout layout = DumpHistoryLayout::Automatic; ///< The requested arrangement.
    DumpHistoryReaderPolicy readers = DumpHistoryReaderPolicy::Reopen; ///< How dumps are opened.
    std::uint32_t blockSamples = 256u; ///< Samples per block for `BlockedPixelMajor`: from 1 through 65536.
    /// The most bytes the history and one decoded region may take together; positive.
    std::uint64_t workingSetBytes = 256u * 1024u * 1024u;
};

/// One complete, context-owned history region. Move-only, no hidden conversions. Samples retain
/// their exact representation and byte order; their canonical index is (channel * height + y) *
/// width + x, even when the input dump interleaves channels. Storage is packed, including the final
/// short block. This object holds a context lease; read-only access is safe concurrently.
class DumpHistory
{
public:
    /// Creates an empty history.
    AFTERNIGHTENGINE_API DumpHistory() noexcept;
    /// Releases the counted history and context lease.
    AFTERNIGHTENGINE_API ~DumpHistory();
    /// Transfers the storage without copying a sample, leaving `other` empty.
    /// @param other the history to take the storage from.
    AFTERNIGHTENGINE_API DumpHistory(DumpHistory&& other) noexcept;
    /// Releases this history's storage, then transfers `other`'s, leaving it empty.
    /// @param other the history to take the storage from.
    /// @return this history.
    AFTERNIGHTENGINE_API DumpHistory& operator=(DumpHistory&& other) noexcept;
    /// Not copyable; a copy would duplicate the whole history implicitly.
    DumpHistory(const DumpHistory&) = delete;
    /// Not copyable; a copy would duplicate the whole history implicitly.
    DumpHistory& operator=(const DumpHistory&) = delete;
    /// Reads one region from every dump, in the order given.
    /// @param engine the engine context that owns the history and each decoded region.
    /// @param execution an execution of that engine context; the reads and copies run inside its task
    ///        arena and check its cancellation.
    /// @param paths the dumps' absolute paths, borrowed for the call; nonempty, and a path may repeat.
    ///        No file may change during the call.
    /// @param expected the description every dump must have exactly, meaning and byte order included.
    /// @param region a nonempty rectangle inside `expected`'s extent; every channel is kept.
    /// @param options the arrangement, the reader policy and the working-set bound.
    /// @param pool the pool for `DumpHistoryReaderPolicy::Pooled`, and null otherwise; it belongs to
    ///        `engine` and outlives the call. Its own storage is not counted in `workingSetBytes`.
    /// @return the complete history - a failure leaves no partial one; `ErrorCode::NotInitialized` for
    ///         an engine context or execution that is not valid; `ErrorCode::IncompatibleArguments` for
    ///         an execution of another engine context or a dump whose description differs from
    ///         `expected`; `ErrorCode::InvalidArgument` for no paths, `blockSamples` or
    ///         `workingSetBytes` out of range, an option outside its enumeration, or a pool given
    ///         without `Pooled` or missing with it; `ErrorCode::OutOfRange` for a region that is empty
    ///         or reaches past the extent; `ErrorCode::LimitExceeded` for a region beyond the engine
    ///         context's limits or a history whose size overflows; `ErrorCode::MemoryBudgetExceeded`
    ///         when the history and one decoded region exceed `workingSetBytes`; the engine context's
    ///         memory refusal; the execution's cancellation; or any failure `DumpReader` or
    ///         `DumpReaderPool` returns while opening or reading a dump.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DumpHistory>
    read(const EngineContext& engine, const ExecutionContext& execution, std::span<const std::string_view> paths,
         const RawFrameDescriptor& expected, const CodecRegion& region, const DumpHistoryOptions& options,
         DumpReaderPool* pool = nullptr);
    /// The arrangement the history resolved to.
    /// @return `FrameMajor` or `BlockedPixelMajor`; `FrameMajor` for an empty history.
    [[nodiscard]] AFTERNIGHTENGINE_API DumpHistoryLayout layout() const noexcept;
    /// Why the history has that arrangement.
    /// @return the reason; `AutomaticReference` for an empty history.
    [[nodiscard]] AFTERNIGHTENGINE_API DumpHistoryLayoutReason layoutReason() const noexcept;
    /// The number of frames held, one per path read.
    /// @return the count, or zero for an empty history.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t frameCount() const noexcept;
    /// The number of samples in one frame's region, every channel counted.
    /// @return the region's width times its height times the channel count, or zero for an empty
    ///         history.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t samplesPerFrame() const noexcept;
    /// One sample's stored bytes, borrowed, under either arrangement. Allocation-free.
    /// @param frameIndex the frame, in the order the paths were given.
    /// @param sampleIndex the sample's canonical planar index within the region.
    /// @return the bytes, valid until the history is moved from or destroyed;
    ///         `ErrorCode::NotInitialized` for an empty history; or `ErrorCode::OutOfRange` for an index
    ///         past the frames or the samples.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<std::span<const std::byte>> sample(std::uint64_t frameIndex,
                                                                                 std::uint64_t sampleIndex) const;
    /// The history's size in memory, as charged to the engine context.
    /// @return the bytes held, or zero for an empty history.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t byteCount() const noexcept;

private:
    struct Impl; ///< Counted memory, geometry and layout selection.
    std::unique_ptr<Impl> m_impl; ///< Exclusive region history.
};
} // namespace an
