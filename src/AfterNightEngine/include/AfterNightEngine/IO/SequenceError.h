// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sequence boundary's typed failure reasons, and how a failure carrying one is built and read.
///
/// A sequence refusal is an ordinary `Status` in `ErrorDomain::Io`: its `ErrorCode` classifies it and a
/// `SequenceFailure` refines it as the typed context field `ContextKey::SequenceReason`, independent
/// of any container library's own error vocabulary. A caller branches on the code and the reason,
/// never on the message. When a backend fails, the checked handle returns a failure with the same code
/// and `SequenceFailure::CallbackFailure` whose cause is the backend's failure, so the backend's own
/// reason is read from `Status::cause()`.

#pragma once

#include <AfterNightEngine/Core/Status.h>

#include <optional>

namespace an
{
/// Why the sequence boundary refused, refining the `ErrorCode` it returned.
enum class SequenceFailure : std::uint8_t
{
    InvalidDescriptor, ///< The format, the timing fields or the advertised capabilities do not cohere.
    Closed, ///< There is no open session, as in a moved-from handle.
    TerminalState, ///< A failed or finished session cannot perform another frame operation.
    UnsupportedSeek, ///< The kind of seek requested is not advertised.
    /// An index, timestamp or duration, or a position a backend resolved, breaks the sequence's
    /// timeline or the seek's rule.
    InvalidPosition,
    DescriptorMismatch, ///< A frame disagrees with the fixed frame description or the engine context's limits.
    UnsafeLifetime, ///< A source returned bare borrowed bytes, with no lifetime token.
    ForeignAllocation, ///< A source's owned bytes were charged to another engine context.
    InvalidMetadata, ///< A source frame's metadata carries an output intent.
    /// A source ended before its stated frame count or right after a seek, or a sink was finished short
    /// of its stated count.
    UnexpectedEnd,
    InvalidReport, ///< A sink reported counts that are not one frame more, or bytes that decreased.
    CallbackFailure, ///< A backend failed or threw; the cause keeps its typed detail.
    MalformedContainer, ///< A container field contradicts its format or another field; Parameter names it.
    UnsupportedEncoding, ///< A container stores, or a request asks to store, what the adapter cannot describe.
    TruncatedContainer, ///< A container holds fewer bytes than it declares; Requested and Available carry both.
    InvalidTimestamps, ///< Timestamps are missing, unrepresentable or decreasing where a timeline was required.
    IoFailure, ///< The operating system refused or shortened an open, read or write; LibraryCode carries its code.
    DecoderFailure, ///< A decoding library failed with no more specific refinement; Library and LibraryCode carry it.
    /// An encoder or muxer failed with no more specific refinement; Library and LibraryCode carry it.
    /// Kept last, because `sequenceFailureReason` treats any larger value as unknown.
    EncoderFailure
};

/// Builds a sequence refusal carrying its reason as typed context, and an optional cause.
///
/// Thread-safe and never throws: when the failure's detail cannot be allocated, the failure carries
/// its code alone, without the reason or the cause.
///
/// @param code the engine-wide classification; `ErrorCode::Ok` is recorded as `ErrorCode::Internal`.
/// @param reason the sequence refinement.
/// @param cause the failure that led to this one, attached unchanged; a success attaches nothing.
/// @return a failure in `ErrorDomain::Io` whose message is the code's standard description.
[[nodiscard]] AFTERNIGHTENGINE_API Status sequenceFailure(ErrorCode code, SequenceFailure reason,
                                                          Status cause = {}) noexcept;

/// The sequence reason a failure carries, read from its typed context without allocating or parsing
/// text; only `status` itself is examined, not its causes.
/// @param status the status to inspect.
/// @return the reason; or no value for a success, a failure outside `ErrorDomain::Io`, a failure with
///         no reason field, or a value past `SequenceFailure::EncoderFailure`.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<SequenceFailure> sequenceFailureReason(const Status& status) noexcept;
} // namespace an
