// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an operation reports when it produces a value that it might not.
///
/// `Status` answers "did it work". `Result<T>` answers "did it work, and what did it produce", which
/// is the same question with the answer attached, so that the value and the reason it is missing
/// cannot drift apart. The alternative shapes both fail in the same way: an out-parameter beside a
/// status leaves a written-or-not question the compiler cannot ask, and a sentinel value inside the
/// return type leaves every call site deciding what the sentinel is.
///
/// C++20 has no `std::expected` - it arrived in C++23 - so this is the project's own, deliberately
/// shaped like it. `hasValue`, `value`, `valueOr`, `operator*` and `operator->` mean what they mean
/// there, and the names differ only where this repository's convention differs. Adopting the
/// standard type when the project moves to C++23 should be a rename, not a redesign.
///
/// Two accessors reach the value on purpose. `operator*` is unchecked and `noexcept`, which is what
/// makes the success path free; `value()` checks and throws. That split is `std::expected`'s, and it
/// is the one place an exception is thrown here at all - reading a result for a value it does not
/// hold is a defect in the caller, not an outcome of the operation, and defects are what exceptions
/// are still for.
///
/// There is deliberately no `Result<void>`. An operation that returns nothing returns `Status`; a
/// specialisation carrying no value would be a second spelling of a type that already exists.

#pragma once

#include <AfterNightEngine/Core/Status.h>

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace an
{

/// Thrown when a `Result` is read for a value it does not hold.
///
/// This is a defect report, not a failure report: the operation already said what went wrong, and
/// the caller read the value anyway. The failure it was carrying is attached, so a crash log says
/// what actually happened rather than only that something was read wrong.
///
/// Only `Result<T>::value()` throws it. `operator*` and `operator->` do not check.
class BadResultAccess : public std::exception
{
public:
    /// Wraps the failure the result was carrying.
    ///
    /// @param status the failure the result held instead of a value. Moved in.
    explicit BadResultAccess(Status status) noexcept : m_status(std::move(status)) {}

    /// What went wrong, in the fixed wording every instance of this defect shares.
    ///
    /// The failure's own message is deliberately not returned here: this text describes the
    /// programming error, and `status()` carries the operation's report. Composing the two would
    /// mean allocating inside an exception that is already being thrown.
    ///
    /// @return a null-terminated string literal that outlives every caller.
    [[nodiscard]] const char* what() const noexcept override
    {
        return "a Result was read for a value it does not hold";
    }

    /// The failure the result was carrying.
    ///
    /// @return the status, valid while this exception object lives.
    [[nodiscard]] const Status& status() const noexcept
    {
        return m_status;
    }

private:
    /// The failure the result held instead of a value.
    Status m_status;
};

/// Either a value or the failure that stopped the operation producing one.
///
/// Marked `[[nodiscard]]` on the type, so every function returning one is checked without each of
/// them having to say so.
///
/// Exactly one of the two states holds: `hasValue()` is true and `status().ok()` is true, or
/// `hasValue()` is false and `status().failed()` is true. There is no third state, and no way to
/// construct one - a `Status` that says success is reported as `ErrorCode::Internal` instead,
/// because a result that holds no value while claiming success is read as a value by everything.
///
/// A value type: it owns its value, copies are independent, and it is as thread-safe as `T` is.
/// Constructing one allocates nothing beyond whatever `T` and the failure's own detail allocate,
/// and the successful path is `noexcept` whenever `T`'s move is.
///
/// @tparam ValueT what the operation produces. Not `void` - an operation returning nothing returns
///         `Status` - not a reference, and not `Status` itself, which would make `Result<Status>`
///         two different answers to one question. Each is refused at compile time.
template <typename ValueT> class [[nodiscard]] Result
{
    static_assert(!std::is_void_v<ValueT>,
                  "an operation that returns nothing returns Status; there is no Result<void>");
    static_assert(!std::is_reference_v<ValueT>, "Result owns its value, so ValueT cannot be a reference");
    static_assert(!std::is_same_v<std::remove_cv_t<ValueT>, Status>,
                  "Result<Status> would carry a failure in both of its states; return Status itself");

public:
    /// What this result produces when it succeeds.
    using ValueType = ValueT;

    /// A result holding a value.
    ///
    /// Implicit, so a function body can `return value;` and read as if it returned the value
    /// directly. The value must already be a `ValueT`: a conversion into `ValueT` and then into
    /// this type would be two user-defined conversions, which the language does not perform.
    ///
    /// @param value the produced value, moved in.
    Result(ValueT value) noexcept(std::is_nothrow_move_constructible_v<ValueT>) : m_value(std::move(value)) {}

    /// A result holding a failure instead of a value.
    ///
    /// Implicit, so a function body can `return Status::failure(...);` and read as if it returned
    /// the failure directly.
    ///
    /// Thread-safe and allocation-free: the failure was already built.
    ///
    /// @param status why no value was produced. A successful status is not an answer to this
    ///        question and is replaced by `ErrorCode::Internal`, so a result that holds nothing
    ///        always says so.
    Result(Status status) noexcept
        : m_status(status.failed() ? std::move(status) : Status::failure(ErrorCode::Internal))
    {
    }

    /// Whether the operation produced a value.
    ///
    /// @return true when a value is held.
    [[nodiscard]] bool hasValue() const noexcept
    {
        return m_value.has_value();
    }

    /// Whether the operation produced a value, in a condition.
    ///
    /// Explicit, so a `Result<bool>` cannot be tested for its value by accident.
    ///
    /// @return true when a value is held.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return hasValue();
    }

    /// The failure, or success.
    ///
    /// @return the status: successful exactly when a value is held. Valid while this result lives.
    [[nodiscard]] const Status& status() const noexcept
    {
        return m_status;
    }

    /// Why no value was produced.
    ///
    /// @return the status's code; `ErrorCode::Ok` when a value is held.
    [[nodiscard]] ErrorCode code() const noexcept
    {
        return m_status.code();
    }

    /// The value, unchecked.
    ///
    /// The accessor the success path uses: `noexcept`, and it does not test anything, which is what
    /// makes it free. Reading it when `hasValue()` is false is undefined behaviour - use `value()`
    /// where the state has not already been established.
    ///
    /// @return a reference to the held value, valid while this result lives and is not reassigned.
    [[nodiscard]] ValueT& operator*() & noexcept
    {
        return *m_value;
    }

    /// The value of a `const` result, unchecked. See the non-`const` overload for the precondition.
    ///
    /// @return a `const` reference to the held value, valid while this result lives.
    [[nodiscard]] const ValueT& operator*() const& noexcept
    {
        return *m_value;
    }

    /// The value of an expiring result, unchecked, ready to be moved from. See the non-`const`
    /// overload for the precondition.
    ///
    /// @return an rvalue reference to the held value.
    [[nodiscard]] ValueT&& operator*() && noexcept
    {
        return std::move(*m_value);
    }

    /// A member of the held value, unchecked. See `operator*` for the precondition.
    ///
    /// @return a pointer to the held value, valid while this result lives.
    [[nodiscard]] ValueT* operator->() noexcept
    {
        return m_value.operator->();
    }

    /// A member of the held value of a `const` result, unchecked. See `operator*` for the
    /// precondition.
    ///
    /// @return a pointer to the held value, valid while this result lives.
    [[nodiscard]] const ValueT* operator->() const noexcept
    {
        return m_value.operator->();
    }

    /// The value, checked.
    ///
    /// The accessor for code that has not already tested the state - a test, a script binding, cold
    /// code where the throw is cheaper than the branch nobody wrote.
    ///
    /// @return a reference to the held value, valid while this result lives.
    /// @throws BadResultAccess when no value is held, carrying the failure that explains why.
    [[nodiscard]] ValueT& value() &
    {
        requireValue();
        return *m_value;
    }

    /// The value of a `const` result, checked.
    ///
    /// @return a `const` reference to the held value, valid while this result lives.
    /// @throws BadResultAccess when no value is held, carrying the failure that explains why.
    [[nodiscard]] const ValueT& value() const&
    {
        requireValue();
        return *m_value;
    }

    /// The value of an expiring result, checked, ready to be moved from.
    ///
    /// @return an rvalue reference to the held value.
    /// @throws BadResultAccess when no value is held, carrying the failure that explains why.
    [[nodiscard]] ValueT&& value() &&
    {
        requireValue();
        return std::move(*m_value);
    }

    /// The value, or a stated substitute.
    ///
    /// For the call sites where a failure has an obvious answer and the alternative is a branch
    /// that says nothing.
    ///
    /// @tparam FallbackT anything convertible to `ValueT`.
    /// @param fallback what to return when no value is held. Evaluated whether or not it is used,
    ///        so it should be cheap; where it is not, branch on `hasValue()` instead.
    /// @return a copy of the held value, or `fallback` converted to `ValueT`.
    template <typename FallbackT> [[nodiscard]] ValueT valueOr(FallbackT&& fallback) const&
    {
        return hasValue() ? *m_value : static_cast<ValueT>(std::forward<FallbackT>(fallback));
    }

    /// The value of an expiring result, or a stated substitute. See the `const` overload; this one
    /// moves the held value out instead of copying it.
    ///
    /// @tparam FallbackT anything convertible to `ValueT`.
    /// @param fallback what to return when no value is held.
    /// @return the held value, moved, or `fallback` converted to `ValueT`.
    template <typename FallbackT> [[nodiscard]] ValueT valueOr(FallbackT&& fallback) &&
    {
        return hasValue() ? std::move(*m_value) : static_cast<ValueT>(std::forward<FallbackT>(fallback));
    }

private:
    /// Refuses a read of a value that is not there, so the three checked accessors state the rule
    /// once between them.
    ///
    /// @throws BadResultAccess when no value is held.
    void requireValue() const
    {
        if (!m_value.has_value())
        {
            throw BadResultAccess(m_status);
        }
    }

    /// The produced value, present exactly when the operation succeeded.
    std::optional<ValueT> m_value;

    /// Why no value was produced; successful exactly when `m_value` holds one.
    Status m_status;
};

} // namespace an
