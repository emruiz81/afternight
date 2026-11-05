// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning an operation's declared effect into the transition its result takes.
///
/// `Image/OperationEffect.h` is what an operation says it will do. This is where that declaration
/// and what the operation actually produced become the one `MetadataTransition` the result's image
/// is given, complete with the lineage entry `Image/Image.h` refuses to move without.
///
/// It is a separate header from the declaration on purpose. A `MetadataTransition` reaches the whole
/// metadata model - six objects, their strings and their vectors - and every operation header in the
/// engine carries a declaration. Keeping the builder next door means a statistics header states its
/// effect without inheriting the metadata model's include closure, which is the same split, for the
/// same reason, as an image's storage naming its engine context without including it.
///
/// **This builds a transition; it does not apply one.** `Image::transition` is still the only door
/// to a new description or new metadata, and it still runs its own checks. What this removes is the
/// step where each call site invents the transition, because that is where a stage ends up recording
/// a change it did not make or omitting one it did.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <optional>
#include <string>

namespace an
{

/// What one completed operation produced, beside the effect it declared.
///
/// The half a declaration cannot carry: a constant says a colour transform restates the meaning,
/// and only the call knows what that meaning became. Every field is checked against the
/// declaration, so an outcome that carries a meaning the operation does not restate is refused
/// rather than silently dropped.
struct OperationOutcome
{
    /// What was done, spelled as the engine spells it - the entry point's own name. Recorded in the
    /// result's lineage. Must not be empty and must be within `kMaxProvenanceTextBytes`.
    std::string operation{};

    /// Anything worth recording beside it: a parameter, a source frame, an acknowledgement.
    /// Recorded in the result's lineage. May be empty; must be within `kMaxProvenanceTextBytes`.
    std::string detail{};

    /// What the result means. Required when the declaration restates the semantics, and refused
    /// otherwise.
    std::optional<ImageSemantics> semantics{};
};

/// Why an outcome cannot be reconciled with the effect its operation declared.
///
/// Each reason names a disagreement between what an operation said it would do and what its call
/// site reported doing. They are separate from `OperationEffectError`, which reads a declaration
/// alone.
enum class OperationOutcomeError : std::uint8_t
{
    /// The outcome matches the declaration.
    None,

    /// The declaration restates the result's meaning and the outcome carries none.
    MissingSemantics,

    /// The outcome carries a meaning for an operation whose declaration preserves it. Stating a
    /// meaning that was not changed would move the result's semantic generation for nothing.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UnexpectedSemantics
};

/// Whether a value is one of the outcome refusals this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isOperationOutcomeError(OperationOutcomeError error) noexcept
{
    return static_cast<std::uint8_t>(error) <= static_cast<std::uint8_t>(OperationOutcomeError::UnexpectedSemantics);
}

/// The refusal's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param error the reason to name. A value outside the enumeration is named
///        `"UnknownOperationOutcomeError"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* operationOutcomeErrorName(OperationOutcomeError error) noexcept;

/// Whether what an operation reported agrees with what it declared.
///
/// Checked before a transition is built, and worth calling on its own by a caller that wants the
/// reason rather than a failure.
///
/// Thread-safe and allocation-free.
///
/// @param effect the operation's published declaration.
/// @param outcome what the completed call produced.
/// @return `OperationOutcomeError::None`, or the first disagreement. Says nothing about whether the
///         declaration itself is consistent; that is `checkOperationEffect`.
[[nodiscard]] AFTERNIGHTENGINE_API OperationOutcomeError
checkOperationOutcome(const OperationEffect& effect, const OperationOutcome& outcome) noexcept;

/// The metadata transition an operation's result takes, given what it declared and what it produced.
///
/// The lineage entry is always present, because `Image::transition` refuses a change that carries
/// none and because an operation that changed nothing still happened. The rest follows the
/// declaration exactly: a preserved meaning states no semantics, an invalidated solution states a
/// default-constructed one, and dropped cards and a withdrawn output intent set their flags.
///
/// `DescriptionEffect` is not read here, because a description is not part of a metadata
/// transition: an operation that restates it gives its new descriptor to the `Image::transition`
/// overload that takes one, beside this transition.
///
/// Thread-safe. Allocates the lineage entry's text and whatever a failure has to describe.
///
/// @param effect the operation's published declaration.
/// @param outcome what the completed call produced.
/// @return the transition, ready for `Image::transition`; or `ErrorCode::InvalidArgument` when the
///         declaration is inconsistent or the outcome disagrees with it - the message naming the
///         `OperationEffectError` or `OperationOutcomeError` at fault - or when the lineage text is
///         empty or beyond `kMaxProvenanceTextBytes`, which `ProvenanceEntry::create` reports naming
///         the field at fault.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition> operationTransition(const OperationEffect& effect,
                                                                                  const OperationOutcome& outcome);

} // namespace an
