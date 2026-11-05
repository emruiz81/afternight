// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reading and writing the persisted star-profile document, in either schema version, bounded and
/// atomic.
///
/// The document's content and its two schemas belong to `Interop/StarProfileDocument.h`; this header
/// only moves a document between a file and memory. A read checks the file's size against the stated
/// bound before it reads a byte, and parses under the same bounds. A write serializes the whole
/// document first, under the same bounds, and publishes it through an `IO/SequenceOutput.h` output - a
/// private pending sibling renamed over the destination once complete - so a reader never meets half a
/// document and a failure leaves the destination as it was.

#pragma once

#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <cstdint>
#include <string_view>

namespace an
{

class EngineContext;
class ExecutionContext;

/// Whether a star-profile payload is flushed before atomic publication.
enum class StarProfileDurability : std::uint8_t
{
    /// Flush the payload to stable storage before publication.
    FlushBeforePublish = 0u,

    /// Close and publish without a device flush.
    CloseOnly = 1u
};

/// What publication does when the star-profile destination already exists.
enum class StarProfileReplacePolicy : std::uint8_t
{
    /// Replace the existing destination atomically.
    ReplaceExisting = 0u,

    /// Refuse the operation before creating a pending sibling.
    RefuseExisting = 1u
};

/// What a caller states when publishing a persisted star profile.
struct StarProfileWriteRequest
{
    /// Final nonempty bounded UTF-8 path, borrowed for the call.
    std::string_view destinationPath;

    /// Whether the payload is flushed before publication.
    StarProfileDurability durability = StarProfileDurability::FlushBeforePublish;

    /// What happens when the destination already exists.
    StarProfileReplacePolicy replacePolicy = StarProfileReplacePolicy::ReplaceExisting;

    /// Bounds also applied to the in-memory record and serialized payload.
    StarProfileReadLimits limits{};
};

/// Settled publication figures for one star-profile document.
struct StarProfileWriteReport
{
    /// Exact serialized payload bytes written before atomic publication.
    std::uint64_t bytesWritten = 0u;
};

/// Reads and validates a version-one or version-two profile document.
///
/// The schema and engine are detected from the JSON object; the filename extension has no role.
/// Every allocation is preceded by the stated byte, star and string bounds, and cancellation is
/// checked before file I/O and after parsing.
///
/// @param execution the operation; its cancellation is checked before the file is read and after it
///        is parsed.
/// @param path the file's UTF-8 path: nonempty, at most 131072 bytes, with no NUL; borrowed for the
///        call.
/// @param limits the file, star and string bounds; each positive.
/// @return the typed record; `ErrorCode::NotInitialized` for an execution that is not valid;
///         `ErrorCode::InvalidArgument` for a path that is not such a path or a zero bound;
///         `ErrorCode::LimitExceeded` when the file or its content exceeds a bound;
///         `ErrorCode::InvalidData` for malformed JSON or a field of the wrong type;
///         `ErrorCode::Unsupported` for a document of another engine or schema version;
///         `ErrorCode::ExternalFailure` with the path, and the operating system's code where it gave
///         one, when the file cannot be inspected, opened or read in full; or the execution's
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarProfileFile>
readStarProfileFile(const ExecutionContext& execution, std::string_view path, const StarProfileReadLimits& limits = {});

/// Serializes and atomically publishes a version-one or version-two profile document.
///
/// The active payload must agree with `profile.generation`. Publication uses the sequence-output
/// transaction, so a failure or cancellation leaves the destination unchanged and removes the
/// private pending sibling.
///
/// @param engine the engine context that owns the output.
/// @param execution an execution of that engine context; its cancellation is checked before and
///        after the document is serialized, and again as the output is committed.
/// @param request the destination, durability, replace policy and bounds.
/// @param profile the record to write; borrowed for the call.
/// @return the committed byte report; `ErrorCode::NotInitialized` for an engine context or execution
///         that is not valid; `ErrorCode::IncompatibleArguments` for an execution of another engine
///         context, or a record whose generation and payload disagree; `ErrorCode::InvalidArgument`
///         for a destination that is not a valid boundary path, a policy outside its enumeration or a
///         zero bound; the validation failures `readStarProfileFile` lists for the record;
///         `ErrorCode::ExternalFailure` when the pending payload cannot be written; what
///         `SequenceOutput::begin` and `SequenceOutput::commit` refuse; or the execution's
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarProfileWriteReport>
writeStarProfileFile(const EngineContext& engine, const ExecutionContext& execution,
                     const StarProfileWriteRequest& request, const StarProfileFile& profile);

} // namespace an
