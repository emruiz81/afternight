// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a generated file reaches its name: whole, or not at all.
///
/// A generated file is written to a pending sibling and renamed into place, and that discipline is
/// what makes header-stamped reuse sound: a file at its destination name is complete by
/// construction, so a reuse decision reads a header instead of hashing a payload. This header makes
/// that discipline one object that every codec goes through, so no format can forget it.
///
/// **Four steps, in a fixed order, each of which can refuse.** `begin` checks the request and
/// reserves the private pending path exclusively, so two writers of one destination meet a refusal
/// rather than each other. `encode` hands the pending path, the effective intent and the reduced
/// metadata to the registry's encoder. `commit` proves the pending payload holds every byte the
/// encoder reported, flushes it to stable storage when the request's durability asks for that, and
/// renames it over the destination in one atomic step. `abort` - or the destructor - removes the
/// pending payload; the destination is never touched by any path but the final rename.
///
/// **Identity travels in the intent and never in the source facts.** A workflow's artifact
/// identity is appended to the caller's output cards; the source's own identity cards are withheld
/// from the encoder, so a product derived from a stamped file carries its own stamp or none, and a
/// reuse check never mistakes it for the file it was derived from.
///
/// **The pending name is derived from the engine context and the operation, not from a process
/// counter.** A global counter is process state nobody owns; the context identity and the
/// operation identity are unique already, and with the process identity in front of them two
/// runs writing one directory cannot collide. A stale reservation from a crashed run with the
/// same three numbers is a conflict, and the sweep in `IO/PendingOutput.h` is the answer to it.
///
/// **An image computed one band at a time is written without being held whole.** In place of
/// `encode`, `reserve` writes the container with room for the image's samples - its header, its
/// metadata and its cards stated from what the image is before any sample exists - and `writeRows`
/// fills that room band by band, from several threads at once for bands that share no row. `commit`
/// publishes such a payload only when every row has been written, so a destination name still never
/// holds a partial image. Only the engine's own adapters can write a reserved image, and today XISF
/// is the one that does: `supportsReservedImageOutput` says which.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace an
{

class EngineContext;
class ExecutionContext;
class ImageLayout;
class RawFrame;
class RawFrameDescriptor;
class OutputTransactionState;

/// Whether the pending payload is flushed to stable storage before it is published.
enum class OutputDurability : std::uint8_t
{
    /// Flush the payload's bytes to the device before the rename, so a power loss after
    /// publication cannot leave a complete name over an incomplete file. The default.
    FlushBeforePublish,

    /// Close and rename without a flush: faster, and correct against a crash of the process but not
    /// of the machine.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CloseOnly
};

/// What happens when the destination already exists.
enum class OutputReplacePolicy : std::uint8_t
{
    /// The rename replaces it, atomically. The default.
    ReplaceExisting,

    /// The transaction is refused at `begin`, before anything is reserved.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RefuseExisting
};

/// Whether a value is one of the durabilities this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param durability the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputDurability(OutputDurability durability) noexcept
{
    return static_cast<std::uint8_t>(durability) <= static_cast<std::uint8_t>(OutputDurability::CloseOnly);
}

/// Whether a value is one of the replace policies this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOutputReplacePolicy(OutputReplacePolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(OutputReplacePolicy::RefuseExisting);
}

/// Something that sees the complete encoded payload before it is published.
///
/// For a caller that digests or archives what it writes. The bytes are read back from the pending
/// payload in bounded chunks after the encoder has finished and before the rename, so what the
/// sink sees is exactly what the destination will hold. A sink must not throw; it handles its own
/// failures, and a transaction reports how many bytes it was shown.
class OutputByteSink
{
public:
    /// Releases whatever the sink holds; must not throw.
    virtual ~OutputByteSink() = default;

    /// One chunk of the payload, in file order.
    ///
    /// @param bytes borrowed for the duration of the call only.
    virtual void observe(std::span<const std::byte> bytes) noexcept = 0;
};

/// What a caller states when it writes an image to a destination.
///
/// A plain aggregate with a default for everything but the destination.
struct OutputRequest
{
    /// The final path, nonempty UTF-8 without NUL, at most 131072 bytes, borrowed for `begin`.
    /// Its directory must exist. The pending sibling is placed beside it, so the rename is a
    /// same-volume operation.
    std::string_view destinationPath;

    /// The container to write, or `CodecFormat::Unknown` to resolve the destination's extension.
    CodecFormat format = CodecFormat::Unknown;

    /// How the samples are to be written, borrowed until `begin` returns. Null asks for the
    /// representation-preserving raw encoding `IO/CodecIO.h` describes, which can carry no cards
    /// and therefore no identity.
    const OutputIntent* intent = nullptr;

    /// The identity this file satisfies, or null for a file that is not a workflow artifact.
    /// Requires an intent to travel in. Borrowed until `begin` returns.
    const ArtifactIdentity* identity = nullptr;

    /// Whether the payload is flushed to the device before publication.
    OutputDurability durability = OutputDurability::FlushBeforePublish;

    /// What happens when the destination already exists.
    OutputReplacePolicy replacePolicy = OutputReplacePolicy::ReplaceExisting;

    /// Something that sees the complete payload before publication, or null. Borrowed until
    /// `commit` returns.
    OutputByteSink* byteSink = nullptr;
};

/// What a committed transaction reports.
struct OutputReport
{
    /// The payload's size, which the encoder reported and the pending file was proved to hold.
    std::uint64_t bytesWritten = 0u;

    /// How many bytes the sink was shown; zero when no sink was given.
    std::uint64_t observedBytes = 0u;

    /// Whether the payload was flushed to the device before publication.
    bool flushed = false;

    /// Whether the destination existed when `begin` checked it and was replaced. This is not
    /// a commit-time observation: another writer can publish between `begin` and `commit`.
    bool replacedExisting = false;

    /// The intent the encoder was given: the caller's, with the identity cards appended. Absent
    /// for the raw encoding.
    std::optional<OutputIntent> intent{};

    /// What the written image's metadata should now say: the effective intent, when there is one,
    /// and a lineage entry naming the destination, the format, the size and the identity. The
    /// caller applies it; the transaction owns no image.
    MetadataTransition transition{};
};

/// One generated file's journey from a private pending payload to its destination name.
///
/// Move-only, and the destructor aborts a transaction that was neither committed nor aborted.
/// Holds a lease on the engine context, so the context refuses to shut down while a payload is
/// pending. The engine and execution contexts are borrowed and must outlive the transaction; the
/// execution's cancellation is checked at every step, and a stop observed after the encoder ran
/// aborts rather than publishes.
class OutputTransaction
{
public:
    /// A transaction that holds nothing.
    ///
    /// The state one is left in by a move, by `abort()` and by `commit()`, and a usable default for
    /// a member assigned later. `valid()` is false and every operation refuses.
    AFTERNIGHTENGINE_API OutputTransaction() noexcept;

    /// Aborts when nothing has committed or aborted already.
    AFTERNIGHTENGINE_API ~OutputTransaction();

    /// Takes over another transaction's pending payload, leaving it holding nothing.
    ///
    /// @param other the transaction to take over.
    AFTERNIGHTENGINE_API OutputTransaction(OutputTransaction&& other) noexcept;

    /// Aborts this transaction's pending payload, if any, and takes over another's.
    ///
    /// @param other the transaction to take over.
    /// @return this transaction.
    AFTERNIGHTENGINE_API OutputTransaction& operator=(OutputTransaction&& other) noexcept;

    /// Copying is deleted: a pending payload has one owner.
    OutputTransaction(const OutputTransaction&) = delete;

    /// Copying is deleted. See the copy constructor.
    OutputTransaction& operator=(const OutputTransaction&) = delete;

    /// Checks the request, resolves the codec, and reserves the private pending path.
    ///
    /// Nothing is written to the destination. The pending path is created exclusively and empty,
    /// so a second transaction for the same destination in the same operation, or a reservation an
    /// earlier run abandoned, is refused rather than overwritten. The identity cards are appended
    /// to the intent here, so a card limit is met before any file exists.
    ///
    /// @param engine the context owning the codec registry; borrowed for the life of the
    ///        transaction and kept open by a lease.
    /// @param execution the operation this write belongs to; borrowed for the life of the
    ///        transaction.
    /// @param request what to write where; borrowed for this call, except as its members say.
    /// @return the transaction; `ErrorCode::NotInitialized` for an engine context or execution that
    ///         is not valid; `ErrorCode::IncompatibleArguments` when the execution belongs to another
    ///         context or an identity is stated without an intent; `ErrorCode::InvalidArgument` for a
    ///         malformed path or an enumerator outside its enumeration, with
    ///         `CodecFailure::UnknownFormat` for the format; `ErrorCode::LimitExceeded` when the
    ///         pending path would exceed the path bound or the cards the card bound;
    ///         `ErrorCode::Unsupported` with `CodecFailure::UnknownFormat` when no format is stated and
    ///         the extension names none, with `CodecFailure::Unavailable` when the engine context has
    ///         no adapter for it, or with `CodecFailure::UnsupportedOperation` when the adapter has no
    ///         encoder; `ErrorCode::InvalidState` with `CodecFailure::ExistingDestination` under the
    ///         refuse policy; `ErrorCode::Busy` with `CodecFailure::PendingConflict` when the pending
    ///         path exists; `ErrorCode::ExternalFailure` with `CodecFailure::DestinationUnavailable`
    ///         and the operating system's code when the reservation is refused - a missing directory
    ///         included; or the execution's cancellation.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<OutputTransaction>
    begin(const EngineContext& engine, const ExecutionContext& execution, const OutputRequest& request);

    /// Serializes the frame into the pending payload through the registry's encoder.
    ///
    /// The encoder is handed the pending path, the effective intent and `outputMetadata` of the
    /// given metadata. May be called once; a failure or a cancellation observed after the encoder
    /// ran removes the pending payload and leaves the transaction holding nothing.
    ///
    /// @param frame the complete samples, borrowed for the call.
    /// @param metadata the image's metadata, reduced for the destination before the encoder sees it.
    /// @return success; `ErrorCode::InvalidState` when the transaction holds nothing or has encoded
    ///         already; the execution's cancellation; or the failure reducing the metadata, or the
    ///         codec facade's, reported - see `encodeImage` in `IO/CodecIO.h`.
    [[nodiscard]] AFTERNIGHTENGINE_API Status encode(const RawFrame& frame, const ImageMetadata& metadata);

    /// Writes the container into the pending payload with room for an image's samples, which
    /// `writeRows` then fills.
    ///
    /// The way to write an image whose rows are computed a band at a time - a stack's master, tile by
    /// tile - without holding it whole. Everything but the samples is written here, from what the
    /// image is and the metadata it will carry: the header, the structural facts and every card, so
    /// nothing about the image may depend on its samples. The intent must state the descriptor's
    /// sample type and byte order, as `encode` requires of a frame, and a form the container writes in
    /// place: no compression, since a compressed block cannot be filled where it lies, and no output
    /// scaling. May be called once, instead of `encode`. A failure removes the pending payload and
    /// leaves the transaction holding nothing.
    ///
    /// @param descriptor what the image is: its geometry, representation and meaning. Its byte order
    ///        must be the host's.
    /// @param arrangement how its samples are stored, `StorageArrangement::Planar` or
    ///        `StorageArrangement::Interleaved`; the rows `writeRows` is given are in it.
    /// @param metadata the image's metadata, reduced for the destination before the adapter sees it.
    /// @return success; `ErrorCode::InvalidState` when the transaction holds nothing or has encoded or
    ///         reserved already; `ErrorCode::Unsupported` with `CodecFailure::UnsupportedOperation` when
    ///         the destination's adapter writes no reserved image, or with
    ///         `CodecFailure::UnsupportedRepresentation` for a representation, an arrangement or an
    ///         intent it cannot write in place; `ErrorCode::IncompatibleArguments` when the intent does
    ///         not state the descriptor's representation; `ErrorCode::LimitExceeded` for an image
    ///         larger than the engine context's limits; the execution's cancellation; or the adapter's
    ///         failure.
    [[nodiscard]] AFTERNIGHTENGINE_API Status reserve(const RawFrameDescriptor& descriptor,
                                                      StorageArrangement arrangement, const ImageMetadata& metadata);

    /// Writes a band of whole rows of the reserved image where they lie in the pending payload.
    ///
    /// Safe to call from several threads at once for bands that share no row, and each row is
    /// written once. The band is read through `layout`, whose padding is skipped: its extent is the
    /// image's width and channels over the band's rows, its element size the descriptor's, and its
    /// arrangement the reserved one. The bytes reach the file before the call returns and nothing of
    /// them is kept. A row write starts no parallel work of its own. `commit`, `abort` and the
    /// destructor must not run while a row write is in progress.
    ///
    /// @param firstRow the image row the band begins at.
    /// @param layout where the band's samples sit.
    /// @param samples the band's bytes, at least `layout.totalBytes()` of them, borrowed for the call.
    /// @return success; `ErrorCode::InvalidState` when the transaction holds no reserved image, or a
    ///         row write of it has failed; `ErrorCode::OutOfRange` for a band outside the image;
    ///         `ErrorCode::IncompatibleArguments` for a layout of another width, channel count, element
    ///         size or arrangement, or samples shorter than it; `ErrorCode::InvalidArgument` naming the
    ///         first of the band's rows written already, in which case nothing is written; the
    ///         execution's cancellation; or the adapter's failure. After a failure of the adapter part
    ///         of the band may have been written, every later row write and the commit refuse, and the
    ///         abort or the destructor removes the payload.
    [[nodiscard]] AFTERNIGHTENGINE_API Status writeRows(std::uint32_t firstRow, const ImageLayout& layout,
                                                        std::span<const std::byte> samples);

    /// Whether this transaction holds a reserved image whose rows `writeRows` takes.
    ///
    /// @return true between a successful `reserve` and `commit` or `abort`.
    [[nodiscard]] AFTERNIGHTENGINE_API bool reserved() const noexcept;

    /// Proves, flushes and publishes the pending payload.
    ///
    /// The pending file must hold exactly the bytes the encoder reported. It is then shown to the
    /// byte sink, flushed under the requested durability, and renamed over the destination in one
    /// step. On any failure the pending payload is removed and the destination is exactly as it
    /// was. Afterwards the transaction holds nothing.
    ///
    /// A reserved image is completed first: every one of its rows must have been written, and the
    /// adapter then finishes the payload - a checksum the container states is computed over the rows
    /// written - and reports its size, which the pending file is held to as an encoder's report is.
    ///
    /// On Windows a rename refused by a sharing, lock or access conflict - another program briefly
    /// holding the destination open - is retried every 5 ms for at most 250 ms, with the execution's
    /// cancellation and deadline checked before each attempt and each wait; other platforms try once.
    /// A rename that succeeds is reported as a success even when a stop arrives at the same moment.
    ///
    /// @return the report; `ErrorCode::InvalidState` when nothing was encoded or reserved, when a
    ///         reserved image's row write failed, or when one of its rows was never written, carrying
    ///         the first such row as `ContextKey::Requested`;
    ///         `ErrorCode::InvalidData` with `CodecFailure::ShortWrite` carrying the reported and
    ///         actual sizes, when the payload is missing, short or cannot be read back;
    ///         `ErrorCode::MemoryBudgetExceeded` or `ErrorCode::AllocationFailed` when the byte sink's
    ///         read buffer is refused; `ErrorCode::ExternalFailure` with `CodecFailure::ShortWrite`
    ///         when the reserved payload cannot be closed, with `CodecFailure::FlushFailure` when it
    ///         cannot be flushed, or with `CodecFailure::PublishFailure` when the rename fails -
    ///         each carrying the operating system's code, the last one tried for a retried rename; or
    ///         the execution's cancellation.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<OutputReport> commit();

    /// Removes the pending payload and releases the lease.
    ///
    /// Idempotent, and a no-op after `commit`. The destination is never touched.
    AFTERNIGHTENGINE_API void abort() noexcept;

    /// Whether this transaction holds a pending payload.
    ///
    /// @return false for a default-constructed, moved-from, aborted or committed transaction.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Whether the encoder has run successfully.
    ///
    /// @return true between a successful `encode` and `commit` or `abort`.
    [[nodiscard]] AFTERNIGHTENGINE_API bool encoded() const noexcept;

    /// Whether the payload reached its destination.
    ///
    /// @return true after a successful `commit`, and false again once moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool committed() const noexcept;

    /// The destination the payload is published to.
    ///
    /// @return the UTF-8 path, borrowed from this transaction; empty when it holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view destinationPath() const noexcept;

    /// The private path the encoder writes to.
    ///
    /// @return the UTF-8 path, borrowed from this transaction; empty when it holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view pendingPath() const noexcept;

    /// The container being written.
    ///
    /// @return the resolved format; `CodecFormat::Unknown` when the transaction holds nothing.
    [[nodiscard]] AFTERNIGHTENGINE_API CodecFormat format() const noexcept;

private:
    /// Takes ownership of a state `begin` has already built.
    ///
    /// @param state the reserved transaction.
    explicit OutputTransaction(std::unique_ptr<OutputTransactionState> state) noexcept;

    /// What this transaction holds, or null.
    std::unique_ptr<OutputTransactionState> m_state;
};

/// Whether an engine context's adapter for a format writes a reserved image.
///
/// Only the engine's own adapters can, since the reservation is a private extension of the adapter
/// interface; an adapter registered from outside the engine writes a frame whole, through `encode`.
/// Today the XISF adapter is the one that does.
///
/// Thread-safe and allocation-free.
///
/// @param engine the context whose registry is asked.
/// @param format the destination's format; `CodecFormat::Unknown` is answered false.
/// @return true when `OutputTransaction::reserve` can reach a reservation for the format.
[[nodiscard]] AFTERNIGHTENGINE_API bool supportsReservedImageOutput(const EngineContext& engine,
                                                                    CodecFormat format) noexcept;

/// `begin`, `encode` and `commit` in one call.
///
/// @param engine the context owning the codec registry.
/// @param execution the operation this write belongs to.
/// @param request what to write where.
/// @param frame the complete samples, borrowed for the call.
/// @param metadata the image's metadata, reduced for the destination before the encoder sees it.
/// @return the committed report, or the first step's failure; on failure nothing is published and
///         no pending payload remains.
[[nodiscard]] AFTERNIGHTENGINE_API Result<OutputReport> writeImage(const EngineContext& engine,
                                                                   const ExecutionContext& execution,
                                                                   const OutputRequest& request, const RawFrame& frame,
                                                                   const ImageMetadata& metadata);

} // namespace an
