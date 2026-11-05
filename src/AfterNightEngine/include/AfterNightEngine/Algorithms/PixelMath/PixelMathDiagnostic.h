// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Why an expression was refused, said once and said precisely.
///
/// A caller that wants to underline the offending characters, to tell "you named an image I do not
/// have" from "you closed a parenthesis I never saw" without matching on prose, or to see every
/// problem at once rather than the first, needs the reason as a value - a thrown string serves none
/// of the three.
///
/// So a diagnostic is a value. It carries a closed code a caller may switch on, the half-open span
/// of source it is about, the name it concerns when it concerns one, and the two counts an arity
/// failure is made of. The sentence is derived from those and is for a person; nothing reads it.
///
/// A front end reports every diagnostic it finds rather than the first, because the alternative is a
/// user fixing one typo per compile. What it never does is report a diagnostic that was caused by an
/// earlier one - a parser that resynchronizes badly turns one missing bracket into forty errors, and
/// forty errors is the same as none.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <string>

namespace an
{

/// Why one expression, or one part of one, was refused.
///
/// A closed enumeration, grouped by the stage that raises it: lexical, syntactic, name resolution,
/// then meaning. The grouping is for a reader; nothing depends on the ordering, and `None` is the
/// only value that means nothing was wrong.
enum class PixelMathDiagnosticCode : std::uint8_t
{
    /// No refusal. The value a diagnostic holds when it is reporting success.
    None = 0u,

    /// A block comment was opened and never closed.
    UnterminatedComment,

    /// A character that begins no token of this language.
    UnexpectedCharacter,

    /// Digits and a decimal point that do not spell a number - two points, an exponent with no
    /// digits after it, a lone point.
    MalformedNumber,

    /// A numeric literal outside what a double can hold.
    NumberOutOfRange,

    /// The source is longer than the configured maximum. Refused before it is read, so an
    /// adversarial input costs one comparison rather than one pass.
    SourceTooLong,

    /// A token appeared where the grammar admits none.
    UnexpectedToken,

    /// A parenthesis was opened and not closed, or closed and never opened.
    UnbalancedParenthesis,

    /// A channel selector's bracket was opened and not closed.
    UnbalancedBracket,

    /// An operator has nothing on one side of it.
    MissingOperand,

    /// The source holds no expression at all - it is empty, or it is only comments.
    EmptyExpression,

    /// The expression needs more syntax nodes than the configured maximum.
    TooManyNodes,

    /// The expression nests deeper than the configured maximum. Checked while descending rather
    /// than after, so a deliberately deep input is refused before it can exhaust the stack.
    TooDeeplyNested,

    /// A call has more arguments than the configured maximum.
    TooManyArguments,

    /// The source holds more statements than the configured maximum.
    TooManyStatements,

    /// A channel selector holds something that is not a non-negative whole number.
    MalformedChannelSelector,

    /// The left side of an assignment is not a declared variable.
    AssignmentTargetNotVariable,

    /// A name that is neither a bound image, a declared constant, a declared variable, nor a
    /// function this build knows.
    UnknownName,

    /// A name used as a call that no function in the registry answers to.
    UnknownFunction,

    /// A meta symbol was used and the bindings do not say which image it stands for.
    UnboundMetaSymbol,

    /// A name that belongs to the language was declared as a constant, a variable, or an image.
    ReservedName,

    /// Two bindings claim the same name.
    DuplicateBinding,

    /// A call has a number of arguments the function does not accept. The expected and actual
    /// counts are carried on the diagnostic.
    ArgumentCountMismatch,

    /// A parameter that has to name an image was given an expression that computes one number.
    ImageArgumentRequired,

    /// A channel selector names a channel the image does not have.
    ChannelOutOfRange,

    /// Two images whose samples mean different kinds of number are combined in one expression.
    /// Refused rather than rescaled, because there is no rescaling that is right for every pair.
    MixedNumericDomains,

    /// An image read at the pixel being written does not have the destination's extent. A
    /// differently sized image is still readable through the sampling function, which states its
    /// coordinates.
    MismatchedImageExtent,

    /// The expression names more images than one program will bind.
    TooManyImages,

    /// A function this build knows the name of and cannot compute yet. Distinct from an unknown
    /// name because the answer is different: the expression is not wrong.
    FunctionNotAvailable,

    /// A colour component was asked of an image that does not carry three colour channels.
    ColorImageRequired,

    /// The bindings name no target image, so there is nothing for the expression to be about.
    NoTargetImage,

    /// The expression writes more distinct names than the configured maximum.
    TooManyNames
};

/// How many codes this build knows, counting `None`.
///
/// `TooManyNames` is last, which is what this depends on.
inline constexpr std::size_t kPixelMathDiagnosticCodeCount =
    static_cast<std::size_t>(PixelMathDiagnosticCode::TooManyNames) + 1u;

/// A half-open run of source characters, counted in bytes from the start of the expression.
///
/// Bytes rather than characters because that is what a caller has to index to underline it, and
/// this language's tokens are all in the basic character set.
struct PixelMathSourceSpan
{
    /// First byte the diagnostic is about.
    std::uint32_t offset = 0u;

    /// How many bytes it covers. Zero is a position rather than a run, which is what a missing
    /// closing bracket is about.
    std::uint32_t length = 0u;

    /// Whether two spans cover the same run.
    ///
    /// @param left one span.
    /// @param right the other.
    /// @return true when both the offset and the length agree.
    [[nodiscard]] friend constexpr bool operator==(const PixelMathSourceSpan& left,
                                                   const PixelMathSourceSpan& right) noexcept = default;
};

/// One reason an expression was refused, with everything a caller needs to act on it.
///
/// A plain aggregate. The members a code does not use are held at their neutral values, which is
/// what lets two diagnostics of one code be compared without asking which fields that code reads.
struct PixelMathDiagnostic
{
    /// Which rule refused.
    PixelMathDiagnosticCode code = PixelMathDiagnosticCode::None;

    /// The source this is about.
    PixelMathSourceSpan span{};

    /// The name, function, or token the diagnostic concerns. Empty when it concerns none.
    std::string name{};

    /// How many arguments, channels, or images were required. Zero when the code names no count.
    std::uint32_t expected = 0u;

    /// How many were supplied. Zero when the code names no count.
    std::uint32_t actual = 0u;
};

/// Whether a diagnostic reports a refusal.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param diagnostic the diagnostic to inspect.
/// @return true when its code is anything but `None`.
[[nodiscard]] constexpr bool isPixelMathRefusal(const PixelMathDiagnostic& diagnostic) noexcept
{
    return diagnostic.code != PixelMathDiagnosticCode::None;
}

/// The code's stable name, for tests and structured reports.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param code the code, including `None` and values this build does not know.
/// @return the name, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pixelMathDiagnosticCodeName(PixelMathDiagnosticCode code) noexcept;

/// One sentence describing the diagnostic, for a person.
///
/// Free to change between builds and never parsed. It names the span, the symbol and the counts the
/// code carries, so the text is derived from the value rather than the value from the text.
///
/// Thread-safe and reentrant. Allocates the returned string.
///
/// @param diagnostic the refusal to describe.
/// @return the sentence, or a statement that nothing was wrong for `None`.
[[nodiscard]] AFTERNIGHTENGINE_API std::string describePixelMathDiagnostic(const PixelMathDiagnostic& diagnostic);

/// The returned failure one diagnostic stands for.
///
/// The code decides the error code: a name the language reserves for something it cannot compute
/// yet is `ErrorCode::NotImplemented`, a configured maximum is `ErrorCode::LimitExceeded`, and
/// everything else is `ErrorCode::InvalidData`, because a refused expression is data that describes
/// something impossible rather than an argument that is out of range. The detail travels as typed
/// context fields - the name as `ContextKey::Parameter` (the code's own name when the diagnostic
/// names nothing), the span's offset as `ContextKey::Completed`, and, when either count is non-zero,
/// the expected and actual counts as `ContextKey::Requested` and `ContextKey::Available` - so a caller
/// acts on those rather than on the message. The span's length does not travel.
///
/// Thread-safe and reentrant.
///
/// @param diagnostic the refusal. `None` produces a successful status.
/// @return the status a front-end entry point returns for this diagnostic.
[[nodiscard]] AFTERNIGHTENGINE_API Status pixelMathDiagnosticStatus(const PixelMathDiagnostic& diagnostic);

} // namespace an
