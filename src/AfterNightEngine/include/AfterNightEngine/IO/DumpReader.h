// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Persistent readers of developer dump files, and a bounded pool that shares them between reads.
///
/// A `DumpReader` opens a dump once, validates its header and exact size, and then serves any number of
/// whole-frame or region reads from the same handle - the shape a streaming stack needs when it reads
/// the same frames again and again. A `DumpReaderPool` keeps up to a stated number of readers open
/// across paths, hands each out exclusively through a `DumpReaderLease`, and waits rather than open a
/// handle past its ceiling. Both report what they did in `DumpIoCounters`.
///
/// A file must not change while a reader has it open. An open handle that sees its file replaced
/// atomically keeps reading the file it opened, and a pool keyed by path would go on serving that old
/// content, so a pool is drained and recreated before its files are replaced. Nothing converts, clips
/// or invents metadata: a read returns the samples as stored, with empty metadata.

#pragma once

#include <AfterNightEngine/IO/CodecRequests.h>

#include <memory>

namespace an
{
class EngineContext;
class ExecutionContext;

/// Aggregate dump I/O measurements. Counts describe validated reader handles and positioned payload
/// transfers, including completed transfers preceding a failed region. All totals saturate.
/// An open refused during header validation never creates a reader and does not enter handle totals.
struct DumpIoCounters
{
    std::uint64_t handleOpens = 0u; ///< Successfully opened and validated readers.
    std::uint64_t handleCloses = 0u; ///< Released file handles.
    std::uint64_t currentOpenHandles = 0u; ///< Currently retained handles.
    std::uint64_t peakOpenHandles = 0u; ///< Maximum simultaneous handles since creation/reset.
    std::uint64_t headerValidations = 0u; ///< Complete version, descriptor and payload-size validations.
    std::uint64_t poolHits = 0u; ///< Reused idle readers for the same immutable path.
    std::uint64_t poolMisses = 0u; ///< Acquisitions requiring a new open.
    std::uint64_t poolEvictions = 0u; ///< Idle readers replaced to respect the handle ceiling.
    std::uint64_t positionedReads = 0u; ///< Successful positioned payload transfers of at most 4 MiB.
    std::uint64_t fullWidthStripReads = 0u; ///< Successful contiguous full-width transfer groups.
    std::uint64_t rowReads = 0u; ///< Successful partial-width row transfer groups.
    std::uint64_t payloadBytes = 0u; ///< Successfully read payload bytes.
};

/// A persistent, move-only reader of one validated version-one dump; it holds a lease on its engine
/// context and one file handle.
///
/// Every call, inspection, move and destruction must be serialized by the caller, except `snapshot`,
/// which may run while a read is in progress.
class DumpReader
{
public:
    /// Creates an empty reader.
    AFTERNIGHTENGINE_API DumpReader() noexcept;
    /// Releases the file, the path's storage and the engine context lease.
    AFTERNIGHTENGINE_API ~DumpReader();
    /// Transfers the open file, leaving `other` empty.
    /// @param other the reader to take the file from.
    AFTERNIGHTENGINE_API DumpReader(DumpReader&& other) noexcept;
    /// Releases this reader's file, then transfers `other`'s, leaving it empty.
    /// @param other the reader to take the file from.
    /// @return this reader.
    AFTERNIGHTENGINE_API DumpReader& operator=(DumpReader&& other) noexcept;
    /// Not copyable; an open file has one owner.
    DumpReader(const DumpReader&) = delete;
    /// Not copyable; an open file has one owner.
    DumpReader& operator=(const DumpReader&) = delete;
    /// Opens a dump and validates its header and exact size, allocating no pixels.
    /// @param engine the engine context that owns the retained path and every frame read.
    /// @param execution an execution of that engine context; cancellation is checked before the file
    ///        is opened, and the open runs inside its task arena.
    /// @param path the dump's absolute UTF-8 path: at most 131072 bytes, with no NUL; copied.
    /// @return the open reader; `ErrorCode::NotInitialized` for an engine context or execution that is
    ///         not valid; `ErrorCode::IncompatibleArguments` for an execution of another engine
    ///         context; `ErrorCode::InvalidArgument` for a path that is empty, too long, holds a NUL or
    ///         is not absolute; the execution's cancellation; the engine context's memory refusal for
    ///         the path's storage; or the failures `createDumpCodec` describes for a read -
    ///         `CodecFailure::MetadataUnreadable` for a file that cannot be opened or a header that is
    ///         not valid, `CodecFailure::PayloadUnreadable` for a file of the wrong size.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DumpReader>
    open(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);
    /// The validated whole-frame description, borrowed without I/O.
    /// @return the description, or null for an empty reader; valid until the reader is moved from or
    ///         destroyed.
    [[nodiscard]] AFTERNIGHTENGINE_API const CodecImageInfo* info() const noexcept;
    /// The path the reader was opened with, borrowed.
    /// @return the path, or empty for an empty reader; valid until the reader is moved from or
    ///         destroyed.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view path() const noexcept;
    /// Reads the whole frame or one rectangle, in positioned transfers of at most 4 MiB.
    /// @param engine the engine context the reader was opened with; it owns the frame.
    /// @param execution an execution of that engine context; the read runs inside its task arena and
    ///        checks cancellation before every transfer.
    /// @param region a nonempty rectangle in the source's row order, or null for the whole frame; only
    ///        the rectangle is allocated and read.
    /// @return the frame, owned by the engine context, with a filter array's phase shifted by the
    ///         region's offset, and empty metadata; `ErrorCode::NotInitialized` for an empty reader;
    ///         `ErrorCode::IncompatibleArguments` for an engine context or execution that is not valid
    ///         or not the reader's; `ErrorCode::OutOfRange` for an empty region or one that reaches past
    ///         the frame; `ErrorCode::InvalidData` with `CodecFailure::PayloadUnreadable` for a file
    ///         that no longer has its validated size or a transfer that comes up short; the engine
    ///         context's memory refusal; or the execution's cancellation.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<CodecDecodeResult>
    read(const EngineContext& engine, const ExecutionContext& execution, const CodecRegion* region = nullptr);
    /// This reader's measurements, taken without waiting for a transfer in progress. It may run
    /// concurrently with `read`, though with no other call. The handle counts are one for an open
    /// reader.
    /// @return a consistent snapshot; zeros for an empty reader.
    [[nodiscard]] AFTERNIGHTENGINE_API DumpIoCounters snapshot() const noexcept;

private:
    struct Impl; ///< File, validated information, counted path bytes and context lease.
    std::unique_ptr<Impl> m_impl; ///< Exclusive session ownership.
};

class DumpReaderPoolState;

/// Exclusive use of one pooled reader, returned to the pool when the lease is destroyed.
///
/// Move-only. A lease stays usable after its pool is drained or destroyed; its reader is then closed
/// when the lease ends. Calls on one lease must be serialized by the caller, while different leases
/// may read concurrently.
class DumpReaderLease
{
public:
    /// Creates an empty lease.
    AFTERNIGHTENGINE_API DumpReaderLease() noexcept;
    /// Returns the reader to its pool, or closes it when the pool has been drained; never throws.
    AFTERNIGHTENGINE_API ~DumpReaderLease();
    /// Transfers exclusive use, leaving `other` empty.
    /// @param other the lease to take the reader from.
    AFTERNIGHTENGINE_API DumpReaderLease(DumpReaderLease&& other) noexcept;
    /// Returns this lease's reader, then transfers `other`'s, leaving it empty.
    /// @param other the lease to take the reader from.
    /// @return this lease.
    AFTERNIGHTENGINE_API DumpReaderLease& operator=(DumpReaderLease&& other) noexcept;
    /// Not copyable; the use is exclusive.
    DumpReaderLease(const DumpReaderLease&) = delete;
    /// Not copyable; the use is exclusive.
    DumpReaderLease& operator=(const DumpReaderLease&) = delete;
    /// The reader this lease holds, borrowed. Do not move from it, replace it or keep it past the lease.
    /// @return the reader, or null for an empty lease.
    [[nodiscard]] AFTERNIGHTENGINE_API DumpReader* reader() const noexcept;

private:
    friend class DumpReaderPool;
    std::shared_ptr<DumpReaderPoolState> m_state; ///< Retains pool slots and allocation owner.
    std::uint32_t m_slot = 0u; ///< Exclusively leased slot.
};

/// A bounded pool of persistent readers over dumps named by absolute path.
///
/// The pool never holds more open handles than its ceiling. An acquire reuses an idle reader of the
/// same path, or evicts the least recently used idle reader, and when every slot is leased it waits -
/// honouring the execution's stop and deadline - rather than open another handle. Two concurrent leases
/// of one path get two independent handles. The slot table and each reader's path are allocated from
/// the engine context. `acquire`, `drain`, `snapshot` and `resetCounters` may be called from any thread;
/// moves and destruction must be serialized by the caller. Draining is final: it wakes every waiter and
/// leaves active leases usable until they are returned.
class DumpReaderPool
{
public:
    /// Creates an empty pool.
    AFTERNIGHTENGINE_API DumpReaderPool() noexcept;
    /// Drains the pool; leases still out keep their own open files.
    AFTERNIGHTENGINE_API ~DumpReaderPool();
    /// Transfers the pool, leaving `other` empty.
    /// @param other the pool to take the slots from.
    AFTERNIGHTENGINE_API DumpReaderPool(DumpReaderPool&& other) noexcept;
    /// Drains this pool, then transfers `other`'s slots, leaving it empty.
    /// @param other the pool to take the slots from.
    /// @return this pool.
    AFTERNIGHTENGINE_API DumpReaderPool& operator=(DumpReaderPool&& other) noexcept;
    /// Not copyable; the slots have one owner.
    DumpReaderPool(const DumpReaderPool&) = delete;
    /// Not copyable; the slots have one owner.
    DumpReaderPool& operator=(const DumpReaderPool&) = delete;
    /// Allocates the slot table from the engine context, opening no file.
    /// @param engine the engine context that owns the slots and every reader; its lease is held until
    ///        the pool and its last lease are gone.
    /// @param maxOpenHandles the ceiling on open handles: from 1 through 4096.
    /// @return the pool; `ErrorCode::NotInitialized` for an engine context that is not valid;
    ///         `ErrorCode::InvalidArgument` for a ceiling outside that range; the engine context's
    ///         memory refusal for the slot table; or `ErrorCode::AllocationFailed`.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DumpReaderPool> create(const EngineContext& engine,
                                                                            std::uint32_t maxOpenHandles);
    /// Takes exclusive use of an idle reader of `path`, or reserves a slot and opens one there.
    /// @param engine the pool's engine context.
    /// @param execution an execution of that engine context, whose stop and deadline end a wait. Call
    ///        it outside a scheduler callback, since waiting blocks the calling thread.
    /// @param path the dump's absolute UTF-8 path, which is also its identity in the pool.
    /// @return the lease; `ErrorCode::NotInitialized` for an empty pool;
    ///         `ErrorCode::IncompatibleArguments` for an engine context or execution that is not valid or
    ///         not the pool's; the execution's cancellation or deadline, checked before and after a
    ///         wait; `ErrorCode::InvalidState` once the pool is drained, including a drain during the
    ///         open; or any failure `DumpReader::open` returns. A failed open returns its slot.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<DumpReaderLease>
    acquire(const EngineContext& engine, const ExecutionContext& execution, std::string_view path);
    /// Closes the idle readers and makes every later acquire fail; active leases stay usable.
    /// @return success, also for a pool already drained; does no cancellable work.
    [[nodiscard]] AFTERNIGHTENGINE_API Status drain() noexcept;
    /// The ceiling on open handles.
    /// @return the ceiling, or zero for an empty pool.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t maxOpenHandles() const noexcept;
    /// The pool's measurements, the transfers of readers now leased included.
    /// @return a consistent snapshot of the handle counts and every reader's transfers; zeros for an
    ///         empty pool.
    [[nodiscard]] AFTERNIGHTENGINE_API DumpIoCounters snapshot() const noexcept;
    /// Resets the totals while no reader is leased; the handles still open become the new baseline of
    /// opens, current and peak handles. Opens and closes no handle.
    /// @return success; `ErrorCode::Busy` while any reader is leased; or `ErrorCode::NotInitialized`
    ///         for an empty pool.
    [[nodiscard]] AFTERNIGHTENGINE_API Status resetCounters() noexcept;

private:
    std::shared_ptr<DumpReaderPoolState> m_state; ///< Context-bound shared slots retained by leases.
};
} // namespace an
