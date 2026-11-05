// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a finished sequence container reaches its name: whole, or not at all.
///
/// A sequence sink writes a container frame by frame, and a container that is half written is worse
/// than none: a reader that finds it at its destination name has no way to tell that the recording
/// stopped. So a sink never writes a destination. It writes the private pending payload this object
/// reserves beside the destination, and publication is a separate step the caller takes once the sink
/// has finished - the same discipline `IO/OutputTransaction.h` applies to a single image, for a payload
/// no image encoder produced.
///
/// **Publication proves the payload before it renames it.** `commit` is given the sink's final report
/// and refuses unless the pending file holds exactly the bytes that report counts, so a sink that
/// failed half way, a payload another process truncated and a report from some other sink are all
/// refused rather than published. The payload is flushed under the stated durability and renamed over
/// the destination in one step; every failure removes it, and the destination is never touched by any
/// path but that rename.
///
/// **A payload has one writer, and the output waits for it.** A sink claims the payload before it opens
/// it. While the claim is held, `commit` refuses - a report from a sink that has not finished matches a
/// payload that is still growing, and on a platform where an open file survives a rename the sink would
/// keep writing into the published name - and an output aborted or destroyed meanwhile leaves removing
/// the payload to the claim, because a file still open for writing cannot always be removed and must not
/// outlive its writer. The order in which a caller lets go of the two is therefore free.
///
/// **The pending name is the image transaction's.** It is derived from the process, the engine context
/// and the operation, carries the shared pending suffix, and is swept by `IO/PendingOutput.h`, so a
/// directory written by both kinds of output has one convention to clean up after a crash.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/SequenceFrame.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace an
{

class EngineContext;
class ExecutionContext;
class SequenceOutputLink;
class SequenceOutputState;

/// What a caller states when a sequence container is to be written to a destination.
///
/// A plain aggregate with a default for everything but the destination.
struct SequenceOutputRequest
{
    /// The final path: nonempty UTF-8 without NUL, at most 131072 bytes, borrowed for `begin`. Its
    /// directory must exist. The pending sibling is placed beside it, so the rename is a same-volume
    /// operation.
    std::string_view destinationPath;

    /// Whether the payload is flushed to the device before publication.
    OutputDurability durability = OutputDurability::FlushBeforePublish;

    /// What happens when the destination already exists.
    OutputReplacePolicy replacePolicy = OutputReplacePolicy::ReplaceExisting;
};

/// What a committed sequence output reports.
struct SequenceOutputReport
{
    /// The container's size, which the sink reported and the pending payload was proved to hold.
    std::uint64_t bytesWritten = 0u;

    /// Whether the payload was flushed to the device before publication.
    bool flushed = false;

    /// Whether the destination existed when `begin` checked it and was replaced. Not a commit-time
    /// observation: another writer can publish between `begin` and `commit`.
    bool replacedExisting = false;
};

/// One writer's hold on an output's pending payload.
///
/// A format sink takes the claim before it opens the payload and keeps it for as long as it may write.
/// While the claim is held the output refuses to commit, and an output aborted or destroyed meanwhile
/// leaves removing the payload to the claim. A writer that finished and closed a complete payload says
/// so with `complete`, after which the output may commit it. Releasing a claim that never completed
/// returns the payload to its output unpublished, and the output may be claimed again.
///
/// Move-only. A claim may be completed or released on another thread than the one calling its output;
/// the hand-over between the two is atomic.
class SequenceOutputClaim
{
public:
    /// A claim that holds nothing.
    AFTERNIGHTENGINE_API SequenceOutputClaim() noexcept;

    /// Releases the claim.
    AFTERNIGHTENGINE_API ~SequenceOutputClaim();

    /// Takes over another claim, leaving it holding nothing.
    ///
    /// @param other the claim to take over.
    AFTERNIGHTENGINE_API SequenceOutputClaim(SequenceOutputClaim&& other) noexcept;

    /// Releases this claim and takes over another.
    ///
    /// @param other the claim to take over.
    /// @return this claim.
    AFTERNIGHTENGINE_API SequenceOutputClaim& operator=(SequenceOutputClaim&& other) noexcept;

    /// A payload has one writer.
    SequenceOutputClaim(const SequenceOutputClaim&) = delete;

    /// A payload has one writer.
    SequenceOutputClaim& operator=(const SequenceOutputClaim&) = delete;

    /// Whether this claim is held.
    ///
    /// @return true from `SequenceOutput::claimWriter` until `release`, a move or destruction.
    [[nodiscard]] AFTERNIGHTENGINE_API bool held() const noexcept;

    /// The pending payload the claim covers.
    ///
    /// @return the UTF-8 path, borrowed from the claim and valid while it is held; empty otherwise.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view pendingPath() const noexcept;

    /// States that the writer finished and closed a complete payload, so the output may commit it.
    ///
    /// Does nothing for a claim that holds nothing, or whose output was aborted or destroyed meanwhile.
    AFTERNIGHTENGINE_API void complete() noexcept;

    /// Gives the payload back.
    ///
    /// A payload that never completed returns to its output unpublished. A payload whose output was
    /// aborted or destroyed while the claim was held is removed here. Idempotent, and never touches a
    /// destination.
    AFTERNIGHTENGINE_API void release() noexcept;

private:
    friend class SequenceOutput;

    /// Holds a claim `SequenceOutput::claimWriter` has already taken.
    ///
    /// @param link the hand-over state shared with the output.
    explicit SequenceOutputClaim(std::shared_ptr<SequenceOutputLink> link) noexcept;

    /// The hand-over state shared with the output, or null.
    std::shared_ptr<SequenceOutputLink> m_link;
};

/// One sequence container's journey from a private pending payload to its destination name.
///
/// Move-only; the destructor aborts an output that was neither committed nor aborted. Holds a lease
/// on the engine context, reserving no thread, from `begin` until it commits or aborts, so the context
/// refuses to shut down while a payload is pending. Every call on one object needs external
/// serialization. A writer claims the payload through `claimWriter`; see `SequenceOutputClaim` for
/// what the claim changes about committing and aborting.
class SequenceOutput
{
public:
    /// An output that holds nothing.
    ///
    /// The state a move, `abort()` and `commit()` leave behind. `valid()` is false and `commit`
    /// refuses.
    AFTERNIGHTENGINE_API SequenceOutput() noexcept;

    /// Aborts when nothing has committed or aborted already.
    AFTERNIGHTENGINE_API ~SequenceOutput();

    /// Takes over another output's pending payload, leaving it holding nothing.
    ///
    /// @param other the output to take over.
    AFTERNIGHTENGINE_API SequenceOutput(SequenceOutput&& other) noexcept;

    /// Aborts this output's pending payload, if any, and takes over another's.
    ///
    /// @param other the output to take over.
    /// @return this output.
    AFTERNIGHTENGINE_API SequenceOutput& operator=(SequenceOutput&& other) noexcept;

    /// Copying is deleted: a pending payload has one owner.
    SequenceOutput(const SequenceOutput&) = delete;

    /// Copying is deleted. See the copy constructor.
    SequenceOutput& operator=(const SequenceOutput&) = delete;

    /// Checks the request and reserves the private pending payload beside the destination.
    ///
    /// The pending path is created exclusively and empty, so a second output for the same
    /// destination in the same operation, or a payload an earlier run abandoned, is refused rather
    /// than overwritten. Nothing is written to the destination.
    ///
    /// @param engine the context the payload is written for; kept open by a lease, never referenced.
    /// @param execution the operation this output belongs to; borrowed for this call.
    /// @param request what is to be written where; borrowed for this call.
    /// @return the output; `ErrorCode::NotInitialized` for an empty context or execution;
    ///         `ErrorCode::IncompatibleArguments` when the execution belongs to another context;
    ///         `ErrorCode::InvalidArgument` naming a malformed path or an enumerator outside its
    ///         enumeration; `ErrorCode::LimitExceeded` when the pending path would exceed the path
    ///         bound; `ErrorCode::InvalidState` with `CodecFailure::ExistingDestination` under the
    ///         refuse policy; `ErrorCode::Busy` with `CodecFailure::PendingConflict` when the pending
    ///         path exists; `ErrorCode::ExternalFailure` with `CodecFailure::DestinationUnavailable`
    ///         carrying the operating system's code; or the execution's cancellation.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<SequenceOutput>
    begin(const EngineContext& engine, const ExecutionContext& execution, const SequenceOutputRequest& request);

    /// Claims the pending payload for one writer.
    ///
    /// @return the claim; or `ErrorCode::InvalidState` when the output holds nothing, a claim is
    ///         already held, or an earlier writer already completed the payload.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceOutputClaim> claimWriter();

    /// Proves, flushes and publishes the pending payload.
    ///
    /// The pending payload must hold exactly `finished.bytesWritten` bytes, and that count must be
    /// positive. It is then flushed under the requested durability and renamed over the destination in
    /// one step. An output that holds nothing, a payload a writer still holds, and an execution of
    /// another context are refused and change nothing; on every later failure the payload is removed,
    /// and the destination is exactly as it was. Afterwards the output holds nothing.
    ///
    /// On Windows a rename refused by a sharing, lock or access conflict is retried every 5 ms for at
    /// most 250 ms, with the execution's cancellation and deadline checked before each attempt and
    /// each wait; other platforms try once. A rename that succeeds is reported as a success even when a
    /// stop arrives at the same moment, and a retry that runs out reports the last platform code.
    ///
    /// @param execution an operation of the same engine context; its cancellation is checked before
    ///        the flush and again before the rename, and a stop observed there publishes nothing.
    /// @param finished the sink's report from its successful finish.
    /// @return the report; `ErrorCode::InvalidState` when the output holds nothing or a writer still
    ///         holds its claim; `ErrorCode::IncompatibleArguments` for an execution that is not valid or
    ///         belongs to another context;
    ///         `ErrorCode::InvalidData` with `CodecFailure::ShortWrite` carrying the reported and
    ///         actual sizes; `ErrorCode::ExternalFailure` with `CodecFailure::FlushFailure` or
    ///         `CodecFailure::PublishFailure` carrying the operating system's code; or the execution's
    ///         cancellation.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SequenceOutputReport> commit(const ExecutionContext& execution,
                                                                           const SequenceWriteResult& finished);

    /// Ends the output without publishing and releases the lease.
    ///
    /// Removes the pending payload, or leaves that to a writer still holding its claim. Idempotent, a
    /// no-op after `commit`, and never touches the destination.
    AFTERNIGHTENGINE_API void abort() noexcept;

    /// Whether this output holds a pending payload.
    ///
    /// @return false for a default-constructed, moved-from, aborted or committed output.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Whether the payload reached its destination.
    ///
    /// @return true after a successful `commit`, and false again once moved from.
    [[nodiscard]] AFTERNIGHTENGINE_API bool committed() const noexcept;

    /// The destination the payload is published to.
    ///
    /// @return the UTF-8 path, borrowed from this output and kept after a commit or an abort; empty for
    ///         a default-constructed or moved-from output.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view destinationPath() const noexcept;

    /// The private path a sink writes the container to.
    ///
    /// @return the UTF-8 path, borrowed from this output and kept after a commit or an abort; empty for
    ///         a default-constructed or moved-from output.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view pendingPath() const noexcept;

    /// Which engine context the payload was reserved for.
    ///
    /// @return the context's identity while the output holds a payload, otherwise zero, which no live
    ///         context has.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t engineContextId() const noexcept;

private:
    /// Takes ownership of a state `begin` has already reserved.
    ///
    /// @param state the reserved output.
    explicit SequenceOutput(std::unique_ptr<SequenceOutputState> state) noexcept;

    /// What this output holds, or null.
    std::unique_ptr<SequenceOutputState> m_state;
};

} // namespace an
