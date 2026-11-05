// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Why a device refused a resource, a mapping, a kernel or a piece of work.
///
/// A device - a GPU, or anything else with memory the host cannot simply read and a queue that
/// finishes work after the call that submitted it has returned - refuses for reasons the shared error
/// codes classify but do not name. "These arguments cannot be used together" is the right code for a
/// kernel bound to host memory and for a transfer that runs off the end of a buffer, and a caller
/// repairs those two differently: one needs a transfer it forgot to declare, the other a smaller
/// range. So every refusal carries the shared code a caller branches on first, and a reason from this
/// closed list for the caller that needs to know which rule it broke. The reason travels as a typed
/// context field; nothing reads it out of a message.
///
/// The reasons belong to the vendor-neutral contract and to nothing else. None of them names an SDK,
/// a driver or a vendor, because the contract they refine is the part of the engine that is kept when
/// any particular device backend is deleted.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace an
{

/// Which rule of the device contract refused, beside the shared `ErrorCode` that classifies it.
///
/// A closed enumeration, carried as `ContextKey::DeviceReason` in a failure's context. The values are
/// contiguous from zero and `BackendFailure` is last, which is what `kDeviceFailureCount` depends on.
enum class DeviceFailure : std::uint8_t
{
    /// A backend described itself incoherently: no residency a kernel can bind, a zero limit, an
    /// alignment ceiling that is not a power of two, or a kernel whose identity is not a known one.
    InvalidCapabilities,

    /// The device handle holds nothing, because it was default-constructed, moved from or shut down.
    DeviceShutDown,

    /// A shutdown was refused because a resource, a mapping, a prepared kernel or accepted work is
    /// still outstanding.
    DeviceInUse,

    /// A resource description is incoherent: zero bytes, an alignment that is not a power of two, or
    /// a layout that needs more bytes or a wider alignment than the description provides.
    InvalidDescriptor,

    /// The backend does not offer the residency a resource asked for.
    ResidencyNotOffered,

    /// A resource asked for more bytes, or a wider alignment, than the backend accepts for one
    /// resource.
    ResourceTooLarge,

    /// A resource handle holds nothing, because it was default-constructed, moved from or released.
    ResourceReleased,

    /// A resource, a prepared kernel or a mapping belongs to a different device from the one asked.
    ForeignDevice,

    /// The host asked to map a resource whose residency the host cannot address.
    NotHostAddressable,

    /// A kernel was bound to a resource whose residency the device cannot read, which is host or
    /// pinned memory; the data reaches the device through a declared transfer instead.
    NotDeviceReachable,

    /// The resource is mapped, so it can be neither mapped a second time nor used by work until the
    /// mapping ends.
    ResourceMapped,

    /// A mapping was refused because accepted work that has not finished still uses the resource.
    PendingUse,

    /// Work uses a resource that accepted, unfinished work writes - or writes a resource that such
    /// work reads - without depending on that work, directly or through its own dependencies.
    UndeclaredDependency,

    /// The device's capabilities do not cover the kernel plan that was asked for.
    KernelNotSupported,

    /// A prepared kernel is incoherent: a backend prepared one whose description disagrees with the
    /// request or with itself, or a submission names a kernel handle that holds nothing.
    InvalidKernel,

    /// A submission's bindings disagree with the kernel's requirements in count, access, size,
    /// alignment or layout.
    BindingMismatch,

    /// Two bindings of one submission name the same resource where the kernel does not permit it.
    BindingAliased,

    /// A transfer names bytes outside the resource it reads or writes.
    TransferOutOfRange,

    /// A transfer reads and writes overlapping bytes of one resource.
    TransferOverlap,

    /// A submission declares no transfer and no kernel, so there is nothing for it to do.
    EmptySubmission,

    /// A submission declares more bindings, transfers or dependencies than the device accepts.
    SubmissionTooLarge,

    /// A dependency is an event handle that holds nothing.
    InvalidDependency,

    /// A dependency finished without succeeding, so the work that needed it was not run. The
    /// dependency's own outcome is the cause.
    DependencyFailed,

    /// The backend released a piece of work without completing it. A defect in the backend.
    WorkAbandoned,

    /// The backend reported success without recording every transfer and the kernel the work
    /// declared. A defect in the backend.
    IncompleteReport,

    /// A chained plan reads a buffer nothing has produced - no earlier step writes it and the host
    /// does not supply it - or names an output no step ever writes.
    UnproducedBuffer,

    /// A chained plan declares a buffer no step binds, so the device would hold memory the chain
    /// never reads or writes.
    UnusedBuffer,

    /// A backend call failed or threw. The backend's own failure is the cause and keeps its typed
    /// detail.
    ///
    /// Deliberately the last enumerator; `kDeviceFailureCount` is derived from it.
    BackendFailure
};

/// How many reasons `DeviceFailure` holds.
///
/// Derived from the last enumerator rather than written out, so that a table sized to the reasons
/// cannot drift from the enumeration while `BackendFailure` stays last.
inline constexpr std::size_t kDeviceFailureCount = static_cast<std::size_t>(DeviceFailure::BackendFailure) + 1u;

/// A device refusal, carrying its reason as a typed context field.
///
/// The failure is raised in `ErrorDomain::Runtime`, whatever the detail said, and the reason is
/// appended after any context the detail already carries. The code a caller branches on is still
/// `code`; the reason refines it and never replaces it.
///
/// Thread-safe. Allocates the failure's detail once, and falls back to a code-only failure rather
/// than throwing when that allocation cannot be made - which loses the reason and keeps the code, the
/// right way round for a failure that may be reporting exhausted memory.
///
/// @param code the shared classification. `ErrorCode::Ok` is not a refusal and is reported as
///        `ErrorCode::Internal`, as every failure factory in the engine does.
/// @param reason which rule of the device contract refused.
/// @param detail the message, operation and further typed context. An empty message is replaced by
///        `deviceFailureDescription(reason)`.
/// @param cause the failure that led to this one, or success when there is none.
/// @return a failure carrying `ContextKey::DeviceReason`, or a code-only failure when its detail could
///         not be allocated.
[[nodiscard]] AFTERNIGHTENGINE_API Status deviceFailure(ErrorCode code, DeviceFailure reason, StatusDetail detail = {},
                                                        Status cause = {}) noexcept;

/// The device reason a failure carries, read without parsing its message.
///
/// Thread-safe and allocation-free.
///
/// @param status the result to inspect.
/// @return the reason; or no value for a success, for a failure outside `ErrorDomain::Runtime`, for a
///         failure that carries no device reason, and for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<DeviceFailure> deviceFailureReason(const Status& status) noexcept;

/// The reason's own name, for a diagnostic field or a report.
///
/// The spelling of the enumerator, stable across builds. A caller still branches on the value.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param reason the reason to name. A value outside the enumeration is named
///        `"UnknownDeviceFailure"` rather than read off the end of a table.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* deviceFailureName(DeviceFailure reason) noexcept;

/// A sentence describing the reason, for a person reading a log.
///
/// It says what the rule is in general, so it is vaguer than a message an operation writes about its
/// own arguments; `deviceFailure` uses it only when it is given no better one.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param reason the reason to describe. A value outside the enumeration gets a sentence saying so.
/// @return a never-null pointer to a string literal compiled into the library.
[[nodiscard]] AFTERNIGHTENGINE_API const char* deviceFailureDescription(DeviceFailure reason) noexcept;

} // namespace an
