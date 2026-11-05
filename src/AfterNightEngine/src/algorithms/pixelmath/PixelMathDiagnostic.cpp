// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the text and the returned failure one diagnostic stands for.
//
// The parser and the resolver both turn their first diagnostic into a status here, so every such
// status names `an::parsePixelMath` as its operation whichever stage raised it; a caller tells the
// stages apart by the diagnostic's code, never by the operation name or the sentence.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <string>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::parsePixelMath";

[[nodiscard]] ErrorCode codeFor(PixelMathDiagnosticCode code) noexcept
{
    switch (code)
    {
    case PixelMathDiagnosticCode::SourceTooLong:
    case PixelMathDiagnosticCode::TooManyNames:
    case PixelMathDiagnosticCode::TooManyNodes:
    case PixelMathDiagnosticCode::TooDeeplyNested:
    case PixelMathDiagnosticCode::TooManyArguments:
    case PixelMathDiagnosticCode::TooManyStatements:
    case PixelMathDiagnosticCode::TooManyImages:
        return ErrorCode::LimitExceeded;
    case PixelMathDiagnosticCode::FunctionNotAvailable:
        return ErrorCode::NotImplemented;
    default:
        break;
    }
    return ErrorCode::InvalidData;
}

[[nodiscard]] std::string decimal(std::uint32_t value)
{
    return std::to_string(value);
}

} // namespace

const char* pixelMathDiagnosticCodeName(PixelMathDiagnosticCode code) noexcept
{
    switch (code)
    {
    case PixelMathDiagnosticCode::None:
        return "none";
    case PixelMathDiagnosticCode::UnterminatedComment:
        return "unterminated-comment";
    case PixelMathDiagnosticCode::UnexpectedCharacter:
        return "unexpected-character";
    case PixelMathDiagnosticCode::MalformedNumber:
        return "malformed-number";
    case PixelMathDiagnosticCode::NumberOutOfRange:
        return "number-out-of-range";
    case PixelMathDiagnosticCode::SourceTooLong:
        return "source-too-long";
    case PixelMathDiagnosticCode::UnexpectedToken:
        return "unexpected-token";
    case PixelMathDiagnosticCode::UnbalancedParenthesis:
        return "unbalanced-parenthesis";
    case PixelMathDiagnosticCode::UnbalancedBracket:
        return "unbalanced-bracket";
    case PixelMathDiagnosticCode::MissingOperand:
        return "missing-operand";
    case PixelMathDiagnosticCode::EmptyExpression:
        return "empty-expression";
    case PixelMathDiagnosticCode::TooManyNames:
        return "TooManyNames";
    case PixelMathDiagnosticCode::TooManyNodes:
        return "too-many-nodes";
    case PixelMathDiagnosticCode::TooDeeplyNested:
        return "too-deeply-nested";
    case PixelMathDiagnosticCode::TooManyArguments:
        return "too-many-arguments";
    case PixelMathDiagnosticCode::TooManyStatements:
        return "too-many-statements";
    case PixelMathDiagnosticCode::MalformedChannelSelector:
        return "malformed-channel-selector";
    case PixelMathDiagnosticCode::AssignmentTargetNotVariable:
        return "assignment-target-not-variable";
    case PixelMathDiagnosticCode::UnknownName:
        return "unknown-name";
    case PixelMathDiagnosticCode::UnknownFunction:
        return "unknown-function";
    case PixelMathDiagnosticCode::UnboundMetaSymbol:
        return "unbound-meta-symbol";
    case PixelMathDiagnosticCode::ReservedName:
        return "reserved-name";
    case PixelMathDiagnosticCode::DuplicateBinding:
        return "duplicate-binding";
    case PixelMathDiagnosticCode::ArgumentCountMismatch:
        return "argument-count-mismatch";
    case PixelMathDiagnosticCode::ImageArgumentRequired:
        return "image-argument-required";
    case PixelMathDiagnosticCode::ChannelOutOfRange:
        return "channel-out-of-range";
    case PixelMathDiagnosticCode::MixedNumericDomains:
        return "mixed-numeric-domains";
    case PixelMathDiagnosticCode::MismatchedImageExtent:
        return "mismatched-image-extent";
    case PixelMathDiagnosticCode::TooManyImages:
        return "too-many-images";
    case PixelMathDiagnosticCode::FunctionNotAvailable:
        return "function-not-available";
    case PixelMathDiagnosticCode::ColorImageRequired:
        return "color-image-required";
    case PixelMathDiagnosticCode::NoTargetImage:
        return "no-target-image";
    }
    return "unknown";
}

std::string describePixelMathDiagnostic(const PixelMathDiagnostic& diagnostic)
{
    const std::string at = " at byte " + decimal(diagnostic.span.offset);
    const std::string named = diagnostic.name.empty() ? std::string{} : (" '" + diagnostic.name + "'");
    switch (diagnostic.code)
    {
    case PixelMathDiagnosticCode::None:
        return "the expression was accepted";
    case PixelMathDiagnosticCode::UnterminatedComment:
        return "a block comment opened" + at + " was never closed";
    case PixelMathDiagnosticCode::UnexpectedCharacter:
        return "the character" + named + at + " begins no token of this language";
    case PixelMathDiagnosticCode::MalformedNumber:
        return "the number" + named + at + " is not written as one";
    case PixelMathDiagnosticCode::NumberOutOfRange:
        return "the number" + named + at + " is outside what this engine can hold";
    case PixelMathDiagnosticCode::SourceTooLong:
        return "the expression is " + decimal(diagnostic.actual) + " bytes long and the limit is " +
               decimal(diagnostic.expected);
    case PixelMathDiagnosticCode::UnexpectedToken:
        return "the token" + named + at + " cannot appear there";
    case PixelMathDiagnosticCode::UnbalancedParenthesis:
        return "a parenthesis" + at + " has no partner";
    case PixelMathDiagnosticCode::UnbalancedBracket:
        return "a channel selector opened" + at + " was never closed";
    case PixelMathDiagnosticCode::MissingOperand:
        return "the operator" + named + at + " has nothing to work on";
    case PixelMathDiagnosticCode::EmptyExpression:
        return "the source holds no expression";
    case PixelMathDiagnosticCode::TooManyNames:
        return "the expression uses more than " + decimal(diagnostic.expected) + " distinct names";
    case PixelMathDiagnosticCode::TooManyNodes:
        return "the expression needs more than " + decimal(diagnostic.expected) + " syntax nodes";
    case PixelMathDiagnosticCode::TooDeeplyNested:
        return "the expression nests deeper" + at + " than the limit of " + decimal(diagnostic.expected);
    case PixelMathDiagnosticCode::TooManyArguments:
        return "the call" + named + at + " has more than " + decimal(diagnostic.expected) + " arguments";
    case PixelMathDiagnosticCode::TooManyStatements:
        return "the source holds more than " + decimal(diagnostic.expected) + " statements";
    case PixelMathDiagnosticCode::MalformedChannelSelector:
        return "the channel selector after" + named + at + " is not a whole number";
    case PixelMathDiagnosticCode::AssignmentTargetNotVariable:
        return "the assignment" + at + " writes to" + named + ", which is not a declared variable";
    case PixelMathDiagnosticCode::UnknownName:
        return "nothing is bound to the name" + named + at;
    case PixelMathDiagnosticCode::UnknownFunction:
        return "this language has no function called" + named + at;
    case PixelMathDiagnosticCode::UnboundMetaSymbol:
        return "the meta symbol" + named + at + " stands for no bound image";
    case PixelMathDiagnosticCode::ReservedName:
        return "the name" + named + " belongs to the language and cannot be bound";
    case PixelMathDiagnosticCode::DuplicateBinding:
        return "the name" + named + " is already bound";
    case PixelMathDiagnosticCode::ArgumentCountMismatch:
        return "the call" + named + at + " was given " + decimal(diagnostic.actual) + " arguments and takes " +
               decimal(diagnostic.expected);
    case PixelMathDiagnosticCode::ImageArgumentRequired:
        return "the first argument of" + named + at + " has to name an image";
    case PixelMathDiagnosticCode::ChannelOutOfRange:
        return "channel " + decimal(diagnostic.actual) + " of" + named + at + " does not exist; it has " +
               decimal(diagnostic.expected);
    case PixelMathDiagnosticCode::MixedNumericDomains:
        return "the image" + named + at + " means a different kind of number from the others in this expression";
    case PixelMathDiagnosticCode::MismatchedImageExtent:
        return "the image" + named + at + " does not have the shape of the image being written";
    case PixelMathDiagnosticCode::TooManyImages:
        return "the expression names more than " + decimal(diagnostic.expected) + " images";
    case PixelMathDiagnosticCode::FunctionNotAvailable:
        return "this build knows" + named + at + " and has no estimator for it";
    case PixelMathDiagnosticCode::ColorImageRequired:
        return "the colour component" + named + at + " needs an image with three colour channels";
    case PixelMathDiagnosticCode::NoTargetImage:
        return "no target image is bound, so the expression has nothing to be about";
    }
    return "the expression was refused for a reason this build does not name";
}

Status pixelMathDiagnosticStatus(const PixelMathDiagnostic& diagnostic)
{
    if (!isPixelMathRefusal(diagnostic))
    {
        return Status::success();
    }
    StatusDetail detail{
        .message = describePixelMathDiagnostic(diagnostic), .operation = kOperation, .domain = ErrorDomain::Algorithms};
    detail.context.push_back(StatusContextField::ofText(
        ContextKey::Parameter,
        diagnostic.name.empty() ? std::string(pixelMathDiagnosticCodeName(diagnostic.code)) : diagnostic.name));
    detail.context.push_back(StatusContextField::ofCount(ContextKey::Completed, diagnostic.span.offset));
    if (diagnostic.expected != 0u || diagnostic.actual != 0u)
    {
        detail.context.push_back(StatusContextField::ofCount(ContextKey::Requested, diagnostic.expected));
        detail.context.push_back(StatusContextField::ofCount(ContextKey::Available, diagnostic.actual));
    }
    return Status::failure(codeFor(diagnostic.code), std::move(detail));
}

} // namespace an
