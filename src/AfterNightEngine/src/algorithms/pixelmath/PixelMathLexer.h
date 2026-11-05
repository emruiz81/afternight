// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - splitting an expression into tokens, and saying where each one was.
//
// Private to the pixel-math domain. A token is a kind, a span and, for the two kinds that carry one,
// a value: nothing here decides what a name refers to, and nothing here builds a tree.
//
// The one decision this stage does make is the one the language forces on it: a name immediately
// followed by an opening parenthesis is a call and a name that is not is a symbol. That has to be
// decided while the characters are in hand - the whitespace between the two is not in the token
// stream - so the token carries the answer rather than the parser guessing at it later.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace an
{

/// What one token is.
enum class PixelMathTokenKind : std::uint8_t
{
    /// A numeric literal, whose value is on the token.
    Number,

    /// A name that is not followed by an opening parenthesis.
    Symbol,

    /// A name that is followed by an opening parenthesis.
    Call,

    /// One of `$T`, `$A` and `$M`.
    MetaSymbol,

    /// `(`.
    LeftParenthesis,

    /// `)`.
    RightParenthesis,

    /// `[`.
    LeftBracket,

    /// `]`.
    RightBracket,

    /// `,`.
    Comma,

    /// `;`.
    Semicolon,

    /// `+`.
    Plus,

    /// `-`.
    Minus,

    /// `--`.
    MinusMinus,

    /// `*`.
    Star,

    /// `/`.
    Slash,

    /// `%`.
    Percent,

    /// `^`.
    Caret,

    /// `~`.
    Tilde,

    /// `!`.
    Bang,

    /// `==`.
    EqualEqual,

    /// `!=`.
    BangEqual,

    /// `<`.
    Less,

    /// `<=`.
    LessEqual,

    /// `>`.
    Greater,

    /// `>=`.
    GreaterEqual,

    /// `&&`.
    AmpersandAmpersand,

    /// `||`.
    PipePipe,

    /// `=`.
    Assign,

    /// `+=`.
    PlusAssign,

    /// `-=`.
    MinusAssign,

    /// `--=`.
    MinusMinusAssign,

    /// `*=`.
    StarAssign,

    /// `/=`.
    SlashAssign,

    /// `%=`.
    PercentAssign,

    /// `^=`.
    CaretAssign,

    /// A character, meta symbol or number the lexer refused. Carried so the parser stops rather than
    /// reading past it.
    Invalid,

    /// One past the last character of the source.
    End
};

/// One token of an expression.
struct PixelMathToken
{
    /// What this token is.
    PixelMathTokenKind kind = PixelMathTokenKind::End;

    /// Where it was written.
    PixelMathSourceSpan span{};

    /// A `Number`'s value. Zero elsewhere.
    double number = 0.0;

    /// The spelling of a `Number`, `Symbol`, `Call` or `MetaSymbol`, borrowed from the source.
    /// Empty for every other kind.
    std::string_view text{};

    /// A `MetaSymbol`'s symbol. `Target` elsewhere.
    PixelMathMetaSymbol metaSymbol = PixelMathMetaSymbol::Target;
};

/// What one pass of the lexer produced.
struct PixelMathTokenStream
{
    /// The tokens, always ending with one of kind `End`.
    std::vector<PixelMathToken> tokens;

    /// Every refusal - an unexpected character or meta symbol, a malformed or out-of-range number,
    /// a block comment never closed - in source order.
    std::vector<PixelMathDiagnostic> diagnostics;
};

/// Splits an expression into tokens.
///
/// The source is borrowed for the call and the returned tokens' text points into it, so the stream
/// must not outlive the source. Lexing does not stop at the first refusal: an unknown character is
/// reported, carried as an `Invalid` token, and lexing continues after it, so one stray character
/// does not hide the rest. An unclosed block comment is reported and consumes the rest of the source
/// without a token.
///
/// @param source the expression's text.
/// @return the tokens and every refusal.
[[nodiscard]] PixelMathTokenStream lexPixelMath(std::string_view source);

} // namespace an
