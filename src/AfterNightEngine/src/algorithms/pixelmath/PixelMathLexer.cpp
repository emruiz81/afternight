// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the character-level half of the pixel-math front end.
//
// The lexical grammar, in the order the loop tries it: whitespace is skipped; `//` runs to the end of
// the line and `/* */` to its close; a digit, or a point before a digit, starts a number of digits
// with at most one point and an optional signed exponent; an ASCII letter or underscore starts a
// name of letters, digits and underscores; `$` followed by `T`, `A` or `M` and no further name
// character is a meta symbol; and anything else is an operator, taken by its longest spelling.
// Everything is ASCII and nothing consults the locale, so one expression lexes one way everywhere.

#include "algorithms/pixelmath/PixelMathLexer.h"

#include <charconv>
#include <string>
#include <system_error>

namespace an
{
namespace
{

[[nodiscard]] constexpr bool isSpace(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\r' || character == '\n' || character == '\v' ||
           character == '\f';
}

[[nodiscard]] constexpr bool isDigit(char character) noexcept
{
    return character >= '0' && character <= '9';
}

[[nodiscard]] constexpr bool isNameStart(char character) noexcept
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
}

[[nodiscard]] constexpr bool isNameCharacter(char character) noexcept
{
    return isNameStart(character) || isDigit(character);
}

/// The lexer's own state, so that the entry point below reads as the language rather than as index
/// arithmetic.
class Lexer
{
public:
    explicit Lexer(std::string_view source) noexcept : m_source(source) {}

    [[nodiscard]] PixelMathTokenStream run();

private:
    [[nodiscard]] char peek(std::size_t ahead = 0u) const noexcept
    {
        const std::size_t at = m_position + ahead;
        return at < m_source.size() ? m_source[at] : '\0';
    }

    [[nodiscard]] bool atEnd() const noexcept
    {
        return m_position >= m_source.size();
    }

    [[nodiscard]] PixelMathSourceSpan spanFrom(std::size_t start) const noexcept
    {
        return PixelMathSourceSpan{static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(m_position - start)};
    }

    void refuse(PixelMathDiagnosticCode code, PixelMathSourceSpan span, std::string name)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.span = span;
        diagnostic.name = std::move(name);
        m_stream.diagnostics.push_back(std::move(diagnostic));
    }

    void emit(PixelMathTokenKind kind, std::size_t start)
    {
        PixelMathToken token;
        token.kind = kind;
        token.span = spanFrom(start);
        m_stream.tokens.push_back(token);
    }

    /// Consumes one operator, choosing the longest spelling that matches.
    void lexOperator();

    /// Consumes one numeric literal, including an exponent when one is written.
    void lexNumber();

    /// Consumes one name, and decides whether it is a call.
    void lexName();

    /// Consumes one meta symbol.
    void lexMetaSymbol();

    /// Consumes a comment, and reports one that is never closed.
    ///
    /// @return true when a comment was consumed.
    bool skipComment();

    std::string_view m_source;
    std::size_t m_position = 0u;
    PixelMathTokenStream m_stream;
};

bool Lexer::skipComment()
{
    if (peek() != '/')
    {
        return false;
    }
    if (peek(1u) == '/')
    {
        while (!atEnd() && m_source[m_position] != '\n')
        {
            ++m_position;
        }
        return true;
    }
    if (peek(1u) == '*')
    {
        const std::size_t start = m_position;
        m_position += 2u;
        while (!atEnd())
        {
            if (m_source[m_position] == '*' && peek(1u) == '/')
            {
                m_position += 2u;
                return true;
            }
            ++m_position;
        }
        refuse(
            PixelMathDiagnosticCode::UnterminatedComment,
            PixelMathSourceSpan{static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(m_source.size() - start)},
            "/*");
        return true;
    }
    return false;
}

void Lexer::lexNumber()
{
    const std::size_t start = m_position;
    bool sawPoint = false;
    bool sawDigit = false;
    while (!atEnd())
    {
        const char character = m_source[m_position];
        if (isDigit(character))
        {
            sawDigit = true;
            ++m_position;
            continue;
        }
        if (character == '.' && !sawPoint)
        {
            sawPoint = true;
            ++m_position;
            continue;
        }
        break;
    }
    bool malformedExponent = false;
    if (!atEnd() && (m_source[m_position] == 'e' || m_source[m_position] == 'E') && sawDigit)
    {
        const std::size_t exponentAt = m_position;
        std::size_t scan = m_position + 1u;
        if (scan < m_source.size() && (m_source[scan] == '+' || m_source[scan] == '-'))
        {
            ++scan;
        }
        if (scan < m_source.size() && isDigit(m_source[scan]))
        {
            m_position = scan;
            while (!atEnd() && isDigit(m_source[m_position]))
            {
                ++m_position;
            }
        }
        else if (scan < m_source.size() && isNameCharacter(m_source[scan]))
        {
            // `1e5x` and `1efoo` are one malformed number rather than a number beside a name: the
            // exponent marker was written and what follows it is not an exponent.
            m_position = scan;
            while (!atEnd() && isNameCharacter(m_source[m_position]))
            {
                ++m_position;
            }
            malformedExponent = true;
        }
        else if (scan >= m_source.size() || !isDigit(m_source[scan]))
        {
            // A bare `e` after a number is the constant's spelling, not an exponent; leave it.
            m_position = exponentAt;
        }
    }

    const std::string_view text = m_source.substr(start, m_position - start);
    const PixelMathSourceSpan span = spanFrom(start);
    if (!sawDigit || malformedExponent)
    {
        refuse(PixelMathDiagnosticCode::MalformedNumber, span, std::string(text));
        emit(PixelMathTokenKind::Invalid, start);
        return;
    }

    // `std::from_chars` for a double is the only conversion in the standard library that neither
    // consults the locale nor sets `errno`, which is what a parser reading untrusted text needs: a
    // decimal point that means something different in another locale would make one expression two.
    // A trailing point is written by people, and it is the one spelling `from_chars` stops short of,
    // so it is trimmed rather than refused.
    std::string_view digits = text;
    if (digits.size() > 1u && digits.back() == '.')
    {
        digits.remove_suffix(1u);
    }
    double value = 0.0;
    const char* const first = digits.data();
    const char* const last = first + digits.size();
    const std::from_chars_result parsed = std::from_chars(first, last, value);
    if (parsed.ec == std::errc::result_out_of_range)
    {
        refuse(PixelMathDiagnosticCode::NumberOutOfRange, span, std::string(text));
        emit(PixelMathTokenKind::Invalid, start);
        return;
    }
    if (parsed.ec != std::errc{} || parsed.ptr != last)
    {
        refuse(PixelMathDiagnosticCode::MalformedNumber, span, std::string(text));
        emit(PixelMathTokenKind::Invalid, start);
        return;
    }

    PixelMathToken token;
    token.kind = PixelMathTokenKind::Number;
    token.span = span;
    token.number = value;
    token.text = text;
    m_stream.tokens.push_back(token);
}

void Lexer::lexName()
{
    const std::size_t start = m_position;
    while (!atEnd() && isNameCharacter(m_source[m_position]))
    {
        ++m_position;
    }
    const std::string_view text = m_source.substr(start, m_position - start);
    const PixelMathSourceSpan span = spanFrom(start);

    std::size_t lookahead = m_position;
    while (lookahead < m_source.size() && isSpace(m_source[lookahead]))
    {
        ++lookahead;
    }
    const bool isCall = lookahead < m_source.size() && m_source[lookahead] == '(';

    PixelMathToken token;
    token.kind = isCall ? PixelMathTokenKind::Call : PixelMathTokenKind::Symbol;
    token.span = span;
    token.text = text;
    m_stream.tokens.push_back(token);
}

void Lexer::lexMetaSymbol()
{
    const std::size_t start = m_position;
    const char which = peek(1u);
    PixelMathMetaSymbol symbol = PixelMathMetaSymbol::Target;
    bool known = true;
    switch (which)
    {
    case 'T':
        symbol = PixelMathMetaSymbol::Target;
        break;
    case 'A':
        symbol = PixelMathMetaSymbol::Active;
        break;
    case 'M':
        symbol = PixelMathMetaSymbol::Mask;
        break;
    default:
        known = false;
        break;
    }
    if (!known || isNameCharacter(peek(2u)))
    {
        // Consume the whole run so the diagnostic names what was written rather than one character
        // of it, and so lexing resumes at something that could start a token.
        m_position += 1u;
        while (!atEnd() && isNameCharacter(m_source[m_position]))
        {
            ++m_position;
        }
        const PixelMathSourceSpan span = spanFrom(start);
        refuse(PixelMathDiagnosticCode::UnexpectedCharacter, span,
               std::string(m_source.substr(start, m_position - start)));
        emit(PixelMathTokenKind::Invalid, start);
        return;
    }

    m_position += 2u;
    PixelMathToken token;
    token.kind = PixelMathTokenKind::MetaSymbol;
    token.span = spanFrom(start);
    token.metaSymbol = symbol;
    token.text = m_source.substr(start, 2u);
    m_stream.tokens.push_back(token);
}

void Lexer::lexOperator()
{
    const std::size_t start = m_position;
    const char character = m_source[m_position];
    const char next = peek(1u);
    const char third = peek(2u);

    const auto take = [&](std::size_t length, PixelMathTokenKind kind) {
        m_position += length;
        emit(kind, start);
    };

    switch (character)
    {
    case '(':
        take(1u, PixelMathTokenKind::LeftParenthesis);
        return;
    case ')':
        take(1u, PixelMathTokenKind::RightParenthesis);
        return;
    case '[':
        take(1u, PixelMathTokenKind::LeftBracket);
        return;
    case ']':
        take(1u, PixelMathTokenKind::RightBracket);
        return;
    case ',':
        take(1u, PixelMathTokenKind::Comma);
        return;
    case ';':
        take(1u, PixelMathTokenKind::Semicolon);
        return;
    case '~':
        take(1u, PixelMathTokenKind::Tilde);
        return;
    case '=':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::EqualEqual : PixelMathTokenKind::Assign);
        return;
    case '+':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::PlusAssign : PixelMathTokenKind::Plus);
        return;
    case '-':
        if (next == '-' && third == '=')
        {
            take(3u, PixelMathTokenKind::MinusMinusAssign);
            return;
        }
        if (next == '-')
        {
            take(2u, PixelMathTokenKind::MinusMinus);
            return;
        }
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::MinusAssign : PixelMathTokenKind::Minus);
        return;
    case '*':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::StarAssign : PixelMathTokenKind::Star);
        return;
    case '/':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::SlashAssign : PixelMathTokenKind::Slash);
        return;
    case '%':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::PercentAssign : PixelMathTokenKind::Percent);
        return;
    case '^':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::CaretAssign : PixelMathTokenKind::Caret);
        return;
    case '<':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::LessEqual : PixelMathTokenKind::Less);
        return;
    case '>':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::GreaterEqual : PixelMathTokenKind::Greater);
        return;
    case '!':
        take(next == '=' ? 2u : 1u, next == '=' ? PixelMathTokenKind::BangEqual : PixelMathTokenKind::Bang);
        return;
    case '&':
        if (next == '&')
        {
            take(2u, PixelMathTokenKind::AmpersandAmpersand);
            return;
        }
        break;
    case '|':
        if (next == '|')
        {
            take(2u, PixelMathTokenKind::PipePipe);
            return;
        }
        break;
    default:
        break;
    }

    ++m_position;
    refuse(PixelMathDiagnosticCode::UnexpectedCharacter, spanFrom(start), std::string(1u, character));
    emit(PixelMathTokenKind::Invalid, start);
}

PixelMathTokenStream Lexer::run()
{
    while (!atEnd())
    {
        const char character = m_source[m_position];
        if (isSpace(character))
        {
            ++m_position;
            continue;
        }
        if (character == '/' && skipComment())
        {
            continue;
        }
        if (isDigit(character) || (character == '.' && isDigit(peek(1u))))
        {
            lexNumber();
            continue;
        }
        if (isNameStart(character))
        {
            lexName();
            continue;
        }
        if (character == '$')
        {
            lexMetaSymbol();
            continue;
        }
        lexOperator();
    }

    PixelMathToken end;
    end.kind = PixelMathTokenKind::End;
    end.span = PixelMathSourceSpan{static_cast<std::uint32_t>(m_source.size()), 0u};
    m_stream.tokens.push_back(end);
    return std::move(m_stream);
}

} // namespace

PixelMathTokenStream lexPixelMath(std::string_view source)
{
    Lexer lexer(source);
    return lexer.run();
}

} // namespace an
