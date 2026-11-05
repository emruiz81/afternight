// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The closed vocabulary every failure is reported in.
///
/// A caller decides what to do about a failure by looking at its code. That is the whole reason this
/// enumeration is closed and small: a code is a promise that two unrelated operations failing for
/// the same reason say so with the same value, and a vocabulary that grows a member per call site
/// stops being something anybody can switch on. The message beside the code explains; it is written
/// for a human, it is free to change between builds, and nothing is allowed to parse it: a caller or
/// a test that matches a substring of a message is coupled to its wording, and that coupling is what
/// this separation exists to prevent.
///
/// The category is the second half of the same idea. Most callers do not care which of three
/// argument mistakes was made - they care that it was the caller's mistake rather than the machine
/// running out of memory or the user pressing stop - so the category answers that question once
/// instead of every call site rebuilding it from a list of codes. Cancellation is its own category
/// for exactly that reason: an operation the user stopped is not an error to report, and telling it
/// apart from a real failure must not depend on remembering two code names.
///
/// The domain says which part of the engine raised the failure. It is an enumeration rather than a
/// string because it is read by code - a diagnostic sink routing by subsystem, a report grouping by
/// area - while the operation name beside it is read by people.

#pragma once

#include <AfterNightEngine/Core/Api.h>

#include <cstddef>
#include <cstdint>

namespace an
{

/// The kind of failure a code describes, for callers that act on the kind rather than the code.
///
/// Every `ErrorCode` belongs to exactly one category, and the mapping is fixed: a code never
/// changes category, because a caller branching on the category would silently change behaviour if
/// one did.
enum class ErrorCategory : std::uint8_t
{
    /// No failure. The category of `ErrorCode::Ok` and of nothing else.
    None,

    /// The caller passed something this operation cannot accept. The call site is what has to
    /// change; retrying with the same arguments produces the same answer.
    Argument,

    /// The object or the engine was not in a state that admits the call - used before it was
    /// initialised, initialised twice, or asked for something out of order.
    State,

    /// The engine cannot do this at all, here: an unimplemented operation, a backend this host does
    /// not have, a combination that is out of scope. Distinct from `Argument` because no change of
    /// argument makes it succeed on this build.
    Capability,

    /// Something the operation needed was not available: memory, a configured budget, a limit. The
    /// same call may succeed later or on a larger machine, which is what makes it worth telling
    /// apart from an argument mistake.
    Resource,

    /// The caller stopped it. A cancelled operation and one that ran out of its deadline are both
    /// here, because neither is a defect and neither belongs in an error report.
    Cancellation,

    /// The data itself is wrong: inconsistent, malformed, or describing something impossible. The
    /// arguments were acceptable; what they pointed at was not.
    Data,

    /// Something outside the engine failed - a library, the operating system, the filesystem. What
    /// it said is carried as context rather than interpreted into engine semantics here.
    External,

    /// A defect in the engine. Reaching one of these means an invariant this code is responsible
    /// for did not hold, and no caller can do anything about it except report it.
    Internal
};

/// Which part of the engine raised a failure.
///
/// Read by code rather than by people: a diagnostic sink routing by subsystem, or a report grouping
/// failures by area. The operation name carried beside it in `StatusDetail` is the half written for
/// a reader.
enum class ErrorDomain : std::uint8_t
{
    /// Not stated. The value a failure carries when nothing set one, so an unset domain stays
    /// distinguishable from a deliberate choice.
    Unspecified,

    /// Core value types: sizes, geometry, limits, and the result model itself.
    Core,

    /// Allocation and the memory budget.
    Memory,

    /// The engine and execution contexts, scheduling, cancellation, diagnostics.
    Runtime,

    /// The image model: descriptors, storage, views, metadata.
    Image,

    /// Processing algorithms.
    Algorithms,

    /// Reading and writing files.
    Io,

    /// Adapters to external libraries.
    Interop
};

/// Why an operation failed.
///
/// A closed enumeration: a caller may switch on it, and the set grows only by a deliberate addition
/// here rather than by a call site inventing a value. The values are contiguous from zero and
/// `Internal` is last, which is what `kErrorCodeCount` depends on.
///
/// Adding a code means adding it to `errorCategory` and to the text tables in `ErrorCode.cpp`. None
/// of those switches carries a `default` label, so a code with no category or no name fails the
/// build rather than reaching a caller as a blank.
enum class ErrorCode : std::uint16_t
{
    /// No failure. The only code for which `Status::ok()` is true.
    Ok = 0,

    /// An argument is outside what this operation accepts, for a reason with no more specific code.
    InvalidArgument,

    /// An index, count, or coordinate is outside the range the operation defines for it.
    OutOfRange,

    /// Each argument is acceptable on its own and they cannot be used together - a mask that does
    /// not match its image, a stride that does not match its element size.
    IncompatibleArguments,

    /// Two buffers overlap where an operation requires them to be disjoint. Named separately from
    /// incompatible values because the caller's repair is to change storage ownership or choose a
    /// supported in-place entry point, not to reinterpret either argument.
    Aliasing,

    /// The object or engine has not been initialised, so there is nothing to operate on yet.
    NotInitialized,

    /// The object or engine is already initialised, and initialising it again would discard state
    /// something else is using.
    AlreadyInitialized,

    /// The object is in a state that does not admit this call, for a reason with no more specific
    /// code - including a call made after the operation finished or after it was moved from.
    InvalidState,

    /// The call is refused because work is still outstanding, and the same call is expected to
    /// succeed once that work finishes. Distinct from `InvalidState` because the caller's answer
    /// is different: wait for what it started and ask again, rather than fix the call site.
    Busy,

    /// This build does not support what was asked for, and no change of argument makes it.
    Unsupported,

    /// The requested backend or instruction set is not available on this host, or was not compiled
    /// into this build. Distinct from `Unsupported` because another host may well have it.
    UnsupportedBackend,

    /// The operation exists in the interface and has no implementation yet. Distinct from
    /// `Unsupported` because it names work not done rather than a deliberate exclusion.
    NotImplemented,

    /// The allocator refused a request. The machine said no, not the engine's own policy; the
    /// requested size is carried as context where it is known.
    AllocationFailed,

    /// The request fits in memory and not within the engine's configured memory budget. A caller
    /// can answer this one - by tiling, by streaming, or by raising the budget - which is why it is
    /// not reported as an allocation failure.
    MemoryBudgetExceeded,

    /// A configured maximum refused the request: a dimension, a channel count, an image size. The
    /// limit and the requested value are carried as context.
    LimitExceeded,

    /// The caller's stop request was observed and the operation gave up at a checkpoint. Not an
    /// error: nothing about the engine or the data is wrong.
    Cancelled,

    /// The operation's time budget ran out before it finished. Told apart from `Cancelled` because
    /// a deadline is a policy the caller set in advance and a cancellation is a decision it made
    /// during the run, and a report treats them differently.
    DeadlineExceeded,

    /// The data itself is inconsistent, malformed, or describes something impossible.
    InvalidData,

    /// Nothing was selected for the operation to work on: every sample it would have read means
    /// nothing under the image's validity rule, or the region it was given holds none. Told apart
    /// from `InvalidData` because nothing about the data is wrong - a frame in which no pixel
    /// survives a rejection is a frame, and the answer is usually to stop using it rather than to
    /// go and fix it.
    EmptySelection,

    /// Something outside the engine failed. What it said - its own status code, its message, the
    /// library it came from - is carried as context, never interpreted into engine semantics here.
    ExternalFailure,

    /// A defect in the engine: an invariant this code is responsible for did not hold. Also the
    /// code a failure is reported under when it was built without one, so an incoherent failure is
    /// loud rather than silently successful.
    ///
    /// Deliberately the last enumerator; `kErrorCodeCount` is derived from it.
    Internal
};

/// How many codes `ErrorCode` holds, counting `Ok`.
///
/// Derived from the last enumerator rather than written out, so it cannot drift from the
/// enumeration while `Internal` stays last. It exists so a diagnostic table - a counter per code, a
/// per-code rate limit - can be sized without its author restating the enumeration.
inline constexpr std::size_t kErrorCodeCount = static_cast<std::size_t>(ErrorCode::Internal) + 1u;

/// The category a code belongs to.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param code the code to classify. A value outside the enumeration is reported as
///        `ErrorCategory::Internal`, because a code this build does not know is a defect somewhere
///        rather than a kind of failure it can describe.
/// @return the category, which is fixed for the life of a code.
[[nodiscard]] constexpr ErrorCategory errorCategory(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return ErrorCategory::None;
    case ErrorCode::InvalidArgument:
    case ErrorCode::OutOfRange:
    case ErrorCode::IncompatibleArguments:
    case ErrorCode::Aliasing:
        return ErrorCategory::Argument;
    case ErrorCode::NotInitialized:
    case ErrorCode::AlreadyInitialized:
    case ErrorCode::InvalidState:
    case ErrorCode::Busy:
        return ErrorCategory::State;
    case ErrorCode::Unsupported:
    case ErrorCode::UnsupportedBackend:
    case ErrorCode::NotImplemented:
        return ErrorCategory::Capability;
    case ErrorCode::AllocationFailed:
    case ErrorCode::MemoryBudgetExceeded:
    case ErrorCode::LimitExceeded:
        return ErrorCategory::Resource;
    case ErrorCode::Cancelled:
    case ErrorCode::DeadlineExceeded:
        return ErrorCategory::Cancellation;
    case ErrorCode::InvalidData:
    case ErrorCode::EmptySelection:
        return ErrorCategory::Data;
    case ErrorCode::ExternalFailure:
        return ErrorCategory::External;
    case ErrorCode::Internal:
        return ErrorCategory::Internal;
    }
    return ErrorCategory::Internal;
}

/// Whether a code says the caller stopped the operation rather than that something went wrong.
///
/// The question a log or a progress report asks before deciding whether it is looking at an error.
/// Both `Cancelled` and `DeadlineExceeded` answer true, which is the point: neither is a defect,
/// and no call site should have to remember both names to say so.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param code the code to test.
/// @return true when the code's category is `ErrorCategory::Cancellation`.
[[nodiscard]] constexpr bool isCancellation(ErrorCode code) noexcept
{
    return errorCategory(code) == ErrorCategory::Cancellation;
}

/// The code's own name, for a log line or a diagnostic field.
///
/// The spelling of the enumerator: `ErrorCode::MemoryBudgetExceeded` is `"MemoryBudgetExceeded"`.
/// Stable, and safe to write into a machine-read diagnostic field - unlike a message, which exists
/// to be rewritten. A caller still branches on the code itself rather than on this text.
///
/// Thread-safe and allocation-free.
///
/// @param code the code to name. A value outside the enumeration is named `"UnknownErrorCode"`,
///        which is what a code from a newer build looks like here.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* errorCodeName(ErrorCode code) noexcept;

/// A sentence describing the code, used as a failure's message when it carries no better one.
///
/// Written for a person reading a log. It says what the code means in general, so it is necessarily
/// vaguer than a message an operation writes about its own arguments; where both exist,
/// `Status::message()` returns the operation's own.
///
/// Thread-safe and allocation-free.
///
/// @param code the code to describe. A value outside the enumeration gets a sentence saying so.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* errorCodeDescription(ErrorCode code) noexcept;

/// The category's own name, for a log line or a diagnostic field.
///
/// Thread-safe and allocation-free.
///
/// @param category the category to name. A value outside the enumeration is named
///        `"UnknownErrorCategory"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* errorCategoryName(ErrorCategory category) noexcept;

/// The domain's own name, for a log line or a diagnostic field.
///
/// Thread-safe and allocation-free.
///
/// @param domain the domain to name. A value outside the enumeration is named
///        `"UnknownErrorDomain"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* errorDomainName(ErrorDomain domain) noexcept;

} // namespace an
