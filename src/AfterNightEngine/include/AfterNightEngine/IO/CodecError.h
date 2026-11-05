// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The codec boundary's typed failure reasons, and how a failure carrying one is built and read.
///
/// A codec refusal is an ordinary `Status` in `ErrorDomain::Io`: its `ErrorCode` classifies it, and a
/// `CodecFailure` refines that classification as a typed context field beside the format and the
/// path. The codec facade, the registry, the format adapters and the output transaction all report
/// this way, and a caller branches on the code and the reason, never on the message.
///
/// When an adapter's own call fails, the facade returns a failure with the same code whose reason is
/// `CodecFailure::CallbackFailure` and whose cause is the adapter's failure. `codecFailureReason`
/// reads one level only, so the adapter's own reason is read from `Status::cause()`, or from
/// `Status::rootCause()` when the adapter wrapped a failure of its own.

#pragma once

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace an
{
/// Why the codec boundary refused, refining the `ErrorCode` it returned; carried as a typed context
/// field and never recovered from the message.
enum class CodecFailure : std::uint8_t
{
    /// No format was selected: none was stated and the path's extension names none, or the stated
    /// value is outside the enumeration.
    UnknownFormat,
    Unavailable, ///< The engine context's registry holds no adapter for the selected format.
    /// The adapter advertises no decoder, no encoder or no native region reader for the operation.
    UnsupportedOperation,
    /// The adapter does not advertise the frame's sample representation, channel count or storage
    /// arrangement, or an output intent disagrees with the frame it would describe.
    UnsupportedRepresentation,
    InvalidCapabilities, ///< An adapter's capability record failed `validCodecCapabilities` at registration.
    DuplicateFormat, ///< Two adapters were registered for the same format.
    /// A description disagrees with the one it has to match - a decoded frame with its probe, a layout
    /// with its descriptor in geometry, representation or byte layout - or a probe reports a
    /// disposition the request did not allow.
    DescriptorMismatch,
    UnsafeLifetime, ///< A decoded frame borrows memory with no lifetime token keeping it alive.
    ForeignAllocation, ///< A decoded frame's engine-owned samples were charged to another engine context.
    /// Metadata the boundary cannot accept: source metadata from a probe or decode that carries an
    /// output intent, which only an encode request may state, or metadata a format cannot store.
    InvalidMetadata,
    /// The header or source metadata could not be read, so nothing about the image is claimed.
    MetadataUnreadable,
    /// The header was readable but the payload failed validation; a header-only probe can still read
    /// the source metadata.
    PayloadUnreadable,
    /// An adapter call returned a failure or threw; the failure's cause is the adapter's own, with its
    /// typed detail.
    CallbackFailure,
    InvalidIdentity, ///< An artifact or run identity is empty, too long, or spelled outside the card-safe alphabet.
    PendingConflict, ///< The private pending path already exists: an earlier transaction is running or was never swept.
    DestinationUnavailable, ///< The destination's directory is missing or the operating system refused the reservation.
    ShortWrite, ///< The pending payload holds fewer bytes than the encoder reported, or none at all.
    FlushFailure, ///< The pending payload could not be flushed to stable storage before publication.
    PublishFailure, ///< The pending payload could not replace the destination; the destination is as it was.
    /// The destination exists and the request refused to replace it. Kept last, because
    /// `codecFailureReason` treats any larger value as unknown.
    ExistingDestination
};

/// Builds a codec refusal carrying its reason, format and path as typed context, and an optional
/// cause.
///
/// Allocates the failure's detail and touches no file.
///
/// @param code the engine-wide classification; `ErrorCode::Ok` is recorded as `ErrorCode::Internal`.
/// @param reason the codec refinement.
/// @param format the selected format, or `Unknown` when selection itself failed.
/// @param operation the entry point's name, for a person reading the failure.
/// @param path the UTF-8 path involved, copied into the failure; empty when there is none.
/// @param cause the failure that led to this one, attached unchanged; a success attaches nothing.
/// @return a failure in `ErrorDomain::Io` whose message is the code's standard description.
[[nodiscard]] AFTERNIGHTENGINE_API Status codecFailure(ErrorCode code, CodecFailure reason, CodecFormat format,
                                                       std::string_view operation, std::string_view path = {},
                                                       Status cause = {});

/// The codec reason a failure carries, read from its typed context without allocating or parsing
/// text.
///
/// Only `status` itself is examined, not its causes.
///
/// @param status the status to inspect.
/// @return the reason; or no value for a success, a failure outside `ErrorDomain::Io`, a failure with
///         no reason field, or a value past `CodecFailure::ExistingDestination`.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<CodecFailure> codecFailureReason(const Status& status) noexcept;
} // namespace an
