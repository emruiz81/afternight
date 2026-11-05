// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an operation reports when it cannot do what it was asked.
///
/// A file that is not there, a parameter out of range, a memory budget exceeded, a run the user
/// cancelled: these are outcomes, not surprises. Reporting them by exception makes every caller's
/// error path invisible at the call site and easy to omit, so this engine returns them. Exceptions
/// stay for defects and for conditions with no useful local handling.
///
/// Three properties shape the type, and each of them costs something somewhere else.
///
/// **Success allocates nothing.** A successful `Status` is a code and two null pointers, and so is a
/// failure carrying only a code - which is what an out-of-memory path has to be able to report,
/// because a failure that allocates in order to say the allocator refused has nothing to say. Only a
/// failure that carries detail allocates, once, and a failure is cold by definition.
///
/// **The detail is a real struct with real members**, not an opaque handle. This engine's public
/// interface is a version-locked in-tree ABI - everything that links it is built from this checkout
/// by the same scripts - so hiding a payload behind accessors would buy binary compatibility nothing
/// needs and cost a call per field. The price is that `<string>` and `<vector>` are in the public
/// surface; that price was anticipated when the include budget was set, and it is recorded there.
///
/// **The cause is another `Status`**, held by its own pointer rather than inside the detail. That
/// costs a second shared pointer on every status, including successful ones, and buys a type whose
/// declaration reads in one pass: the alternative is to declare the detail before the status, the
/// status before the detail, and split the accessors around the gap. A chain built this way is
/// acyclic by construction, because every node is immutable once its successor exists.
///
/// What a caller acts on is a code, a category, or a typed context field. Never a message: the
/// message is written for a person, is free to change between builds, and parsing one is the
/// coupling this design exists to prevent.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace an
{

/// What one typed fact about a failure is about.
///
/// The keys are the facts a caller acts on differently, not everything a message might mention. A
/// budget failure that carries `Requested` and `Limit` can be answered by tiling; one that says the
/// same thing only in its message can only be shown to somebody.
enum class ContextKey : std::uint8_t
{
    /// The name of the parameter that was rejected, as text.
    Parameter,

    /// A filesystem path the failure is about, as text. Carried only where it is safe to report.
    Path,

    /// The external library that reported the failure, as text - `"cfitsio"`, `"libtiff"`.
    Library,

    /// The status code that library reported, in its own numbering. Signed, because most such
    /// codes are, and never interpreted into engine semantics.
    LibraryCode,

    /// How much the operation asked for, in the unit the operation works in: bytes for an
    /// allocation, pixels for a dimension, threads for an arena.
    Requested,

    /// How much was actually available, in the same unit as `Requested`.
    Available,

    /// The configured bound or budget that refused it, in the same unit as `Requested`.
    Limit,

    /// How many units the operation finished before it failed. This and `Total` are how a partial
    /// result is described without the failure carrying one.
    Completed,

    /// How many units the operation intended to finish.
    Total,

    /// The container format a codec failure concerns: a `CodecFormat` from `IO/CodecFormat.h`,
    /// stored as a `ContextValueKind::Count`.
    CodecFormat,

    /// Why a codec refused: a `CodecFailure` from `IO/CodecError.h`, stored as a
    /// `ContextValueKind::Count`. It refines the failure's code and is never parsed from a message.
    CodecReason,

    /// Why a sequence source or sink refused: a `SequenceFailure` from `IO/SequenceError.h`,
    /// stored as a `ContextValueKind::Count`.
    SequenceReason,

    /// An optional adapter's own refinement of a failure, stored as a `ContextValueKind::Count`.
    ///
    /// The adapter that raised the failure defines the enumeration and reads it back; the engine
    /// carries the number and never interprets it. One key rather than one per adapter, because a
    /// caller that holds an adapter already knows which vocabulary the value belongs to, and a key
    /// per adapter would put every optional library's name into the engine's closed enumeration.
    AdapterReason,

    /// The presentation index of the frame a streaming run failed on, stored as a
    /// `ContextValueKind::Count`.
    FrameIndex,

    /// The stage a streaming run failed in: a `FrameStreamStage` from `IO/FrameStream.h`, stored as
    /// a `ContextValueKind::Count`.
    StreamStage,

    /// Which device rule refused: a `DeviceFailure` from `Runtime/DeviceError.h`, stored as a
    /// `ContextValueKind::Count`. One key for the whole device contract, because the reason names
    /// which of its rules refused and the shared code already says what kind of refusal it was.
    DeviceReason
};

/// Which of a context field's values carries its meaning.
///
/// Counts preserve the unsigned range, library codes preserve signed integers, and real values
/// preserve fractional parameters and limits without rounding them to integers.
enum class ContextValueKind : std::uint8_t
{
    /// The value is in `StatusContextField::text`.
    Text,

    /// The value is in `StatusContextField::count`.
    Count,

    /// The value is in `StatusContextField::number`.
    Number,

    /// The value is in `StatusContextField::real`.
    Real
};

/// One typed fact attached to a failure.
///
/// A plain aggregate, so a call site can name what it is setting. Exactly one of `text`, `count`,
/// `number` and `real` carries the value, named by `kind`; the others hold their zero. Prefer the
/// static entry points below to filling the members directly - they cannot leave `kind` disagreeing
/// with the member that was set.
struct StatusContextField
{
    /// What this fact is about.
    ContextKey key = ContextKey::Parameter;

    /// Which member below carries the value.
    ContextValueKind kind = ContextValueKind::Text;

    /// The value when `kind` is `ContextValueKind::Count`; zero otherwise.
    std::uint64_t count = 0u;

    /// The value when `kind` is `ContextValueKind::Number`; zero otherwise.
    std::int64_t number = 0;

    /// The value when `kind` is `ContextValueKind::Text`; empty otherwise. Owned, so it may be
    /// built from a temporary and outlives whatever produced it.
    std::string text{};

    /// The value when `kind` is `ContextValueKind::Real`; zero otherwise. May carry a rejected NaN.
    double real = 0.0;

    /// A field carrying a floating-point parameter or limit, without quantization.
    /// @param key what the fact is about.
    /// @param value the value, including non-finite values when those caused the refusal.
    /// @return the field with `kind` set to `ContextValueKind::Real`.
    [[nodiscard]] static StatusContextField ofReal(ContextKey key, double value)
    {
        StatusContextField field;
        field.key = key;
        field.kind = ContextValueKind::Real;
        field.real = value;
        return field;
    }

    /// A field carrying text.
    ///
    /// @param key what the fact is about.
    /// @param value the text; copied into the field, so it need not outlive the call.
    /// @return the field, with `kind` set to `ContextValueKind::Text`.
    [[nodiscard]] static StatusContextField ofText(ContextKey key, std::string value)
    {
        StatusContextField field;
        field.key = key;
        field.kind = ContextValueKind::Text;
        field.text = std::move(value);
        return field;
    }

    /// A field carrying an unsigned count - bytes, pixels, elements, threads.
    ///
    /// @param key what the fact is about.
    /// @param value the count.
    /// @return the field, with `kind` set to `ContextValueKind::Count`.
    [[nodiscard]] static StatusContextField ofCount(ContextKey key, std::uint64_t value)
    {
        StatusContextField field;
        field.key = key;
        field.kind = ContextValueKind::Count;
        field.count = value;
        return field;
    }

    /// A field carrying a signed number - an external library's own status code, an offset, a
    /// difference.
    ///
    /// @param key what the fact is about.
    /// @param value the number.
    /// @return the field, with `kind` set to `ContextValueKind::Number`.
    [[nodiscard]] static StatusContextField ofNumber(ContextKey key, std::int64_t value)
    {
        StatusContextField field;
        field.key = key;
        field.kind = ContextValueKind::Number;
        field.number = value;
        return field;
    }
};

/// Everything a failure says beyond its code.
///
/// A plain aggregate, filled with designated initialisers at the point the failure is built:
///
/// @code
/// return Status::failure(ErrorCode::MemoryBudgetExceeded,
///                        StatusDetail{.message = "the frame does not fit the configured budget",
///                                     .operation = "ImageStorage::allocate",
///                                     .domain = ErrorDomain::Memory,
///                                     .context = {StatusContextField::ofCount(ContextKey::Requested, bytes),
///                                                 StatusContextField::ofCount(ContextKey::Limit, budget)}});
/// @endcode
///
/// Every member is optional, and every one of them carries a default. Naming only the members a
/// failure actually sets is the expected form, and the defaults are what makes it warning-free:
/// clang under `-Wextra` reports a designated initialiser that skips a member without one.
///
/// Once inside a `Status` the detail is immutable and shared between that status's copies, which is
/// what makes copying one cheap and reading one from several threads safe.
struct StatusDetail
{
    /// A sentence for a person, explaining this failure rather than this code. Never parsed, and
    /// free to change between builds. Empty means `Status::message()` falls back to the code's own
    /// description.
    std::string message{};

    /// The engine operation that failed, spelled as it is in the source -
    /// `"EngineContext::create"`. Owned, and for a reader; the domain beside it is the part read
    /// by code.
    std::string operation{};

    /// Which part of the engine raised the failure.
    ErrorDomain domain = ErrorDomain::Unspecified;

    /// Typed facts a caller may act on, in the order they were attached. Usually empty, and never
    /// a substitute for the code: context refines a failure, it does not identify one.
    std::vector<StatusContextField> context{};
};

/// What an operation that returns nothing reports.
///
/// Either success or one failure. Marked `[[nodiscard]]` on the type, so every function returning
/// one is checked without each of them having to say so, and dropping a failure is a compiler
/// diagnostic rather than a review finding.
///
/// A value type: copies are independent, the detail and cause they share are immutable, and two
/// threads may read copies of one status concurrently. Copying and moving are `noexcept`, and
/// nothing here allocates except the factories that are given detail to carry.
///
/// Shaped so that adopting `std::expected`'s error slot later is mechanical rather than a redesign.
class [[nodiscard]] Status
{
public:
    /// Success.
    ///
    /// The default is deliberately the successful state: a status nobody set is a status nothing
    /// went wrong in, and a failure has to be stated.
    Status() noexcept = default;

    /// Success, said out loud.
    ///
    /// The spelling to use at a return site, where `return Status::success();` says what it means
    /// and `return {};` does not.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return a status for which `ok()` is true.
    [[nodiscard]] static Status success() noexcept
    {
        return Status{};
    }

    /// A failure carrying only its code.
    ///
    /// The form an allocation failure has to be able to use: it allocates nothing, so it is
    /// reportable when there is no memory left to report with. `message()` falls back to the code's
    /// own description, so this is a complete failure rather than a blank one.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param code why the operation failed. `ErrorCode::Ok` is not a failure and is reported as
    ///        `ErrorCode::Internal` instead, because a status that claims success while being
    ///        returned as a failure is worse than a wrong code: every caller reads it as success.
    /// @return a status for which `failed()` is true.
    [[nodiscard]] static Status failure(ErrorCode code) noexcept
    {
        Status status;
        status.m_code = code == ErrorCode::Ok ? ErrorCode::Internal : code;
        return status;
    }

    /// A failure carrying a code and a message.
    ///
    /// Thread-safe. Allocates the message and the detail that holds it.
    ///
    /// @param code why the operation failed; `ErrorCode::Ok` is replaced by `ErrorCode::Internal`
    ///        as in the code-only overload.
    /// @param message a sentence for a person, copied into the failure. Never parsed by a caller.
    /// @return a status for which `failed()` is true and `message()` returns this message.
    [[nodiscard]] static Status failure(ErrorCode code, std::string message)
    {
        return failure(code, StatusDetail{.message = std::move(message)});
    }

    /// A failure carrying a code and full detail: message, operation, domain, and typed context.
    ///
    /// Thread-safe. Allocates the detail, once.
    ///
    /// @param code why the operation failed; `ErrorCode::Ok` is replaced by `ErrorCode::Internal`
    ///        as in the code-only overload.
    /// @param detail everything the failure says beyond its code, moved into the status and
    ///        immutable afterwards.
    /// @return a status for which `failed()` is true and `detail()` is not null.
    [[nodiscard]] static Status failure(ErrorCode code, StatusDetail detail)
    {
        Status status = failure(code);
        status.m_detail = std::make_shared<const StatusDetail>(std::move(detail));
        return status;
    }

    /// A failure raised from another failure, keeping both.
    ///
    /// This is how a boundary reports in its own vocabulary without discarding what it was told: an
    /// image load that failed because a codec refused is an `InvalidData` failure caused by an
    /// `ExternalFailure`, and a report can show either level without parsing a message.
    ///
    /// Thread-safe. Allocates the detail and the cause.
    ///
    /// @param code why this operation failed; `ErrorCode::Ok` is replaced by `ErrorCode::Internal`
    ///        as in the code-only overload.
    /// @param detail everything this failure says beyond its code.
    /// @param cause the failure that led to this one, moved in. A successful status is not a cause
    ///        and is dropped, leaving `hasCause()` false.
    /// @return a status for which `failed()` is true and `hasCause()` is true when `cause` was
    ///         itself a failure.
    [[nodiscard]] static Status failure(ErrorCode code, StatusDetail detail, Status cause)
    {
        Status status = failure(code, std::move(detail));
        if (cause.failed())
        {
            status.m_cause = std::make_shared<const Status>(std::move(cause));
        }
        return status;
    }

    /// Why the operation failed, or `ErrorCode::Ok`.
    ///
    /// The value a caller branches on.
    ///
    /// @return the code this status carries.
    [[nodiscard]] ErrorCode code() const noexcept
    {
        return m_code;
    }

    /// The kind of failure this is, for a caller that acts on the kind rather than the code.
    ///
    /// @return the category of `code()`; `ErrorCategory::None` for a successful status.
    [[nodiscard]] ErrorCategory category() const noexcept
    {
        return errorCategory(m_code);
    }

    /// Whether the operation succeeded.
    ///
    /// @return true when the code is `ErrorCode::Ok`.
    [[nodiscard]] bool ok() const noexcept
    {
        return m_code == ErrorCode::Ok;
    }

    /// Whether the operation failed.
    ///
    /// Spelled out rather than left to `!ok()`, because the negation is the form that gets misread
    /// at a glance in a long condition.
    ///
    /// @return true when the code is anything but `ErrorCode::Ok`.
    [[nodiscard]] bool failed() const noexcept
    {
        return m_code != ErrorCode::Ok;
    }

    /// Whether this failure is the caller having stopped the operation.
    ///
    /// True for both `ErrorCode::Cancelled` and `ErrorCode::DeadlineExceeded`. A log that treats
    /// failures as errors asks this first, because neither of those is one.
    ///
    /// @return true when the category is `ErrorCategory::Cancellation`.
    [[nodiscard]] bool isCancellation() const noexcept
    {
        return an::isCancellation(m_code);
    }

    /// What this failure says, for a person.
    ///
    /// The detail's message where there is one, and the code's own description otherwise, so this
    /// is never empty and never null - a caller writing it into a log needs no fallback of its own.
    ///
    /// Never parsed. Anything a caller acts on is the code, the category, or a context field.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return a null-terminated string. It is owned by this status's detail when the detail
    ///         carries a message, so it stays valid while any copy of this status exists; otherwise
    ///         it is a literal compiled into the library and outlives every caller.
    [[nodiscard]] const char* message() const noexcept
    {
        if (m_detail != nullptr && !m_detail->message.empty())
        {
            return m_detail->message.c_str();
        }
        return errorCodeDescription(m_code);
    }

    /// The engine operation that failed.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return a null-terminated string, empty when nothing set one. Valid while any copy of this
    ///         status exists.
    [[nodiscard]] const char* operation() const noexcept
    {
        return m_detail != nullptr ? m_detail->operation.c_str() : "";
    }

    /// Which part of the engine raised this failure.
    ///
    /// @return the domain, or `ErrorDomain::Unspecified` when nothing set one.
    [[nodiscard]] ErrorDomain domain() const noexcept
    {
        return m_detail != nullptr ? m_detail->domain : ErrorDomain::Unspecified;
    }

    /// Whether this status carries detail beyond its code.
    ///
    /// Also the answer to whether building it allocated: a status for which this is false cost
    /// nothing to produce.
    ///
    /// @return true when `detail()` is not null.
    [[nodiscard]] bool hasDetail() const noexcept
    {
        return m_detail != nullptr;
    }

    /// Everything this failure says beyond its code.
    ///
    /// Handed over as the struct itself rather than one accessor per member, because it is a
    /// documented public value and a caller reading several fields should not pay a call for each.
    ///
    /// @return a pointer to the immutable detail, or null when there is none. Valid while any copy
    ///         of this status exists, and safe to read from several threads at once.
    [[nodiscard]] const StatusDetail* detail() const noexcept
    {
        return m_detail.get();
    }

    /// The first context field with a given key.
    ///
    /// The usual way to read context: a caller looking for how much was requested wants that field,
    /// not the vector. Where a key is attached more than once, the first one wins, which is the
    /// order it was attached in.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @param key the fact to look for.
    /// @return a pointer to the field, or null when this failure carries no field with that key.
    ///         Valid while any copy of this status exists.
    [[nodiscard]] const StatusContextField* findContext(ContextKey key) const noexcept
    {
        if (m_detail == nullptr)
        {
            return nullptr;
        }
        for (const StatusContextField& field : m_detail->context)
        {
            if (field.key == key)
            {
                return &field;
            }
        }
        return nullptr;
    }

    /// Whether another failure led to this one.
    ///
    /// @return true when a cause was given and was itself a failure.
    [[nodiscard]] bool hasCause() const noexcept
    {
        return m_cause != nullptr;
    }

    /// The failure that led to this one.
    ///
    /// Returned by value rather than by reference, so walking a chain cannot outlive a link of it.
    /// The copy is two pointer copies; a cause is read when something is being reported, never in a
    /// loop.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the cause, or a successful status when there is none - so `cause().ok()` and
    ///         `!hasCause()` are the same question.
    [[nodiscard]] Status cause() const noexcept
    {
        return m_cause != nullptr ? *m_cause : Status{};
    }

    /// The deepest failure in this one's cause chain.
    ///
    /// What a report shows when it wants the thing that actually went wrong rather than the layer
    /// that noticed. The chain is acyclic by construction - every link is immutable once something
    /// points at it - so this terminates.
    ///
    /// Thread-safe and allocation-free.
    ///
    /// @return the last status in the chain, which is a copy of this one when it has no cause.
    [[nodiscard]] Status rootCause() const noexcept
    {
        Status current = *this;
        while (current.hasCause())
        {
            current = current.cause();
        }
        return current;
    }

private:
    /// Why the operation failed. `ErrorCode::Ok` is the successful state and the only one for which
    /// the two pointers below are guaranteed null.
    ErrorCode m_code = ErrorCode::Ok;

    /// Everything beyond the code, allocated once when a failure is given any, and immutable
    /// afterwards so that copies of this status may share it across threads.
    std::shared_ptr<const StatusDetail> m_detail;

    /// The failure this one was raised from, or null. Held beside the detail rather than inside it
    /// so that this type can be declared in one pass.
    std::shared_ptr<const Status> m_cause;
};

} // namespace an
