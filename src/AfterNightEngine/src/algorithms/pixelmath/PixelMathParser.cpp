// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - recursive descent over the token stream, under stated limits.
//
// The grammar, loosest binding first: statements separated by `;`, where a trailing separator ends
// the sequence; an assignment, which groups to the right and whose target must be a plain name;
// `||`; `&&`; `==` and `!=`; the four orderings; `+`, `-` and `--`, the absolute difference; `*`, `/`
// and `%`; the prefix `+`, `-`, `~` and `!`, which nest; `^`, which groups to the right and whose
// exponent may carry its own prefix; and a primary - a number, a name or meta symbol with an optional
// `[channel]`, a call, or a parenthesised sequence. So `-2^2` is minus four and `2^-1` one half.
//
// Every level that recurses counts itself against the depth limit before it recurses. After a
// refusal the rest of that statement is skipped to the next top-level separator, and only the
// parser's first refusal in a statement is reported, so one mistake is one diagnostic; the lexer's
// refusals are all kept.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>

#include "algorithms/pixelmath/PixelMathLexer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace an
{
namespace
{

/// One level of the descent, counted so that nesting is bounded by a number rather than by the
/// machine's stack. Entering a level that would exceed the limit is refused before the recursive
/// call is made, which is the only ordering under which a deliberately deep input costs a
/// comparison instead of a crash.
class DepthGuard
{
public:
    DepthGuard(std::uint32_t& depth, std::uint32_t& highWater) noexcept : m_depth(depth), m_highWater(highWater)
    {
        ++m_depth;
        if (m_depth > m_highWater)
        {
            m_highWater = m_depth;
        }
    }

    DepthGuard(const DepthGuard&) = delete;
    DepthGuard(DepthGuard&&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    DepthGuard& operator=(DepthGuard&&) = delete;

    ~DepthGuard()
    {
        --m_depth;
    }

private:
    std::uint32_t& m_depth;
    std::uint32_t& m_highWater;
};

} // namespace

/// Builds a syntax tree from a token stream, or the diagnostics that refused it.
///
/// One parser belongs to one parse. It is a class rather than a function because the descent needs
/// state - where it is, how deep it is, what it has built - and threading six references through
/// a dozen mutually recursive functions is how a bound gets forgotten in one of them.
class PixelMathParser
{
public:
    PixelMathParser(std::string_view source, const PixelMathParseLimits& limits) : m_source(source), m_limits(limits)
    {
        m_tree.m_nodes.emplace();
        m_tree.m_children.emplace();
        m_tree.m_names.emplace();
        m_parse.m_diagnostics.emplace();
    }

    [[nodiscard]] PixelMathParse run();

    [[nodiscard]] static PixelMathParse allocationFailure() noexcept
    {
        PixelMathParse refused;
        refused.m_allocationFailed = true;
        return refused;
    }

private:
    static constexpr std::uint32_t kNoNode = 0xFFFFFFFFu;

    [[nodiscard]] const PixelMathToken& current() const noexcept
    {
        return m_tokens.tokens[m_position];
    }

    [[nodiscard]] PixelMathTokenKind kind() const noexcept
    {
        return current().kind;
    }

    void advance() noexcept
    {
        if (m_position + 1u < m_tokens.tokens.size())
        {
            ++m_position;
        }
    }

    void refuse(PixelMathDiagnosticCode code, const PixelMathToken& token, std::uint32_t expected = 0u,
                std::uint32_t actual = 0u)
    {
        if (m_statementFailed)
        {
            // One refusal per statement. Everything after the first is about a state the parser
            // reached only because of it.
            return;
        }
        m_statementFailed = true;
        PixelMathDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.span = token.span;
        diagnostic.name = std::string(token.text);
        diagnostic.expected = expected;
        diagnostic.actual = actual;
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
    }

    void refuseAt(PixelMathDiagnosticCode code, PixelMathSourceSpan span, std::string name, std::uint32_t expected = 0u,
                  std::uint32_t actual = 0u)
    {
        if (m_statementFailed)
        {
            return;
        }
        m_statementFailed = true;
        PixelMathDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.span = span;
        diagnostic.name = std::move(name);
        diagnostic.expected = expected;
        diagnostic.actual = actual;
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
    }

    [[nodiscard]] std::uint32_t internName(std::string_view name)
    {
        const auto found = m_nameIndices.find(std::string(name));
        if (found != m_nameIndices.end())
        {
            return found->second;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(m_tree.m_names->size());
        m_tree.m_names->emplace_back(name);
        m_nameIndices.emplace(std::string(name), index);
        return index;
    }

    [[nodiscard]] std::uint32_t addNode(const PixelMathNode& node)
    {
        if (m_tree.m_nodes->size() >= m_limits.maxNodes)
        {
            refuseAt(PixelMathDiagnosticCode::TooManyNodes, node.span, {}, m_limits.maxNodes,
                     static_cast<std::uint32_t>(m_tree.m_nodes->size()));
            return kNoNode;
        }
        m_tree.m_nodes->push_back(node);
        return static_cast<std::uint32_t>(m_tree.m_nodes->size() - 1u);
    }

    [[nodiscard]] std::uint32_t addNodeWithChildren(PixelMathNode node, std::span<const std::uint32_t> children)
    {
        node.firstChild = static_cast<std::uint32_t>(m_tree.m_children->size());
        node.childCount = static_cast<std::uint32_t>(children.size());
        for (const std::uint32_t child : children)
        {
            m_tree.m_children->push_back(child);
        }
        return addNode(node);
    }

    [[nodiscard]] bool enter(const PixelMathToken& token)
    {
        if (m_depth >= m_limits.maxDepth)
        {
            refuse(PixelMathDiagnosticCode::TooDeeplyNested, token, m_limits.maxDepth, m_depth);
            return false;
        }
        return true;
    }

    /// Skips to the token after the next statement separator, so the next statement is read from a
    /// position the grammar can start at.
    void recover();

    [[nodiscard]] std::uint32_t parseStatements(bool insideParentheses);
    [[nodiscard]] std::uint32_t parseAssignment();
    [[nodiscard]] std::uint32_t parseLogicalOr();
    [[nodiscard]] std::uint32_t parseLogicalAnd();
    [[nodiscard]] std::uint32_t parseEquality();
    [[nodiscard]] std::uint32_t parseComparison();
    [[nodiscard]] std::uint32_t parseAdditive();
    [[nodiscard]] std::uint32_t parseMultiplicative();
    [[nodiscard]] std::uint32_t parseUnary();
    [[nodiscard]] std::uint32_t parsePower();
    [[nodiscard]] std::uint32_t parsePrimary();
    [[nodiscard]] std::uint32_t parseCall(const PixelMathToken& name);
    void parseChannelSelector(PixelMathNode& node);

    std::string_view m_source;
    PixelMathParseLimits m_limits;
    PixelMathTokenStream m_tokens;
    std::size_t m_position = 0u;
    std::uint32_t m_depth = 0u;
    std::uint32_t m_maxDepth = 0u;
    bool m_statementFailed = false;
    std::unordered_map<std::string, std::uint32_t> m_nameIndices;
    PixelMathSyntaxTree m_tree;
    PixelMathParse m_parse;
};

void PixelMathParser::recover()
{
    std::uint32_t nesting = 0u;
    while (kind() != PixelMathTokenKind::End)
    {
        const PixelMathTokenKind at = kind();
        if (at == PixelMathTokenKind::LeftParenthesis || at == PixelMathTokenKind::LeftBracket)
        {
            ++nesting;
        }
        else if (at == PixelMathTokenKind::RightParenthesis || at == PixelMathTokenKind::RightBracket)
        {
            if (nesting == 0u)
            {
                return;
            }
            --nesting;
        }
        else if (at == PixelMathTokenKind::Semicolon && nesting == 0u)
        {
            advance();
            return;
        }
        advance();
    }
}

void PixelMathParser::parseChannelSelector(PixelMathNode& node)
{
    if (kind() != PixelMathTokenKind::LeftBracket)
    {
        return;
    }
    const PixelMathToken opening = current();
    advance();
    if (kind() != PixelMathTokenKind::Number)
    {
        refuse(PixelMathDiagnosticCode::MalformedChannelSelector, current());
        return;
    }
    const PixelMathToken number = current();
    const double value = number.number;
    if (!(value >= 0.0 && value <= 65535.0))
    {
        refuse(PixelMathDiagnosticCode::MalformedChannelSelector, number);
        return;
    }
    const std::uint32_t channel = static_cast<std::uint32_t>(value);
    if (static_cast<double>(channel) != value)
    {
        refuse(PixelMathDiagnosticCode::MalformedChannelSelector, number);
        return;
    }
    advance();
    if (kind() != PixelMathTokenKind::RightBracket)
    {
        refuseAt(PixelMathDiagnosticCode::UnbalancedBracket, opening.span, std::string(opening.text));
        return;
    }
    advance();
    node.channel = channel;
    node.hasChannel = true;
    node.span.length = (number.span.offset + number.span.length + 1u) - node.span.offset;
}

std::uint32_t PixelMathParser::parseCall(const PixelMathToken& name)
{
    const PixelMathToken opening = current();
    advance(); // the opening parenthesis, which the lexer proved is there.

    std::vector<std::uint32_t> arguments;
    if (kind() != PixelMathTokenKind::RightParenthesis)
    {
        while (true)
        {
            const std::uint32_t argument = parseAssignment();
            if (argument == kNoNode)
            {
                return kNoNode;
            }
            if (arguments.size() >= m_limits.maxCallArguments)
            {
                refuseAt(PixelMathDiagnosticCode::TooManyArguments, name.span, std::string(name.text),
                         m_limits.maxCallArguments, static_cast<std::uint32_t>(arguments.size() + 1u));
                return kNoNode;
            }
            arguments.push_back(argument);
            if (kind() != PixelMathTokenKind::Comma)
            {
                break;
            }
            advance();
        }
    }
    if (kind() != PixelMathTokenKind::RightParenthesis)
    {
        refuseAt(PixelMathDiagnosticCode::UnbalancedParenthesis, opening.span, "(");
        return kNoNode;
    }
    const PixelMathToken closing = current();
    advance();

    PixelMathNode node;
    node.kind = PixelMathSyntaxKind::Call;
    node.nameIndex = internName(name.text);
    node.span = PixelMathSourceSpan{name.span.offset, (closing.span.offset + closing.span.length) - name.span.offset};
    return addNodeWithChildren(node, std::span<const std::uint32_t>(arguments));
}

std::uint32_t PixelMathParser::parsePrimary()
{
    const PixelMathToken token = current();
    switch (token.kind)
    {
    case PixelMathTokenKind::Number: {
        advance();
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Number;
        node.number = token.number;
        node.span = token.span;
        return addNode(node);
    }
    case PixelMathTokenKind::Symbol: {
        advance();
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Name;
        node.nameIndex = internName(token.text);
        node.span = token.span;
        parseChannelSelector(node);
        if (m_statementFailed)
        {
            return kNoNode;
        }
        return addNode(node);
    }
    case PixelMathTokenKind::MetaSymbol: {
        advance();
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::MetaSymbol;
        node.metaSymbol = token.metaSymbol;
        node.nameIndex = internName(token.text);
        node.span = token.span;
        parseChannelSelector(node);
        if (m_statementFailed)
        {
            return kNoNode;
        }
        return addNode(node);
    }
    case PixelMathTokenKind::Call: {
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        return parseCall(token);
    }
    case PixelMathTokenKind::LeftParenthesis: {
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        if (kind() == PixelMathTokenKind::RightParenthesis)
        {
            refuse(PixelMathDiagnosticCode::UnexpectedToken, current());
            return kNoNode;
        }
        const std::uint32_t inner = parseStatements(true);
        if (inner == kNoNode)
        {
            return kNoNode;
        }
        if (kind() != PixelMathTokenKind::RightParenthesis)
        {
            refuseAt(PixelMathDiagnosticCode::UnbalancedParenthesis, token.span, "(");
            return kNoNode;
        }
        advance();
        return inner;
    }
    case PixelMathTokenKind::Invalid:
        // The lexer already said what was wrong with it. Stop the statement without a second
        // diagnostic about the same characters.
        m_statementFailed = true;
        return kNoNode;
    default:
        break;
    }

    refuse(PixelMathDiagnosticCode::UnexpectedToken, token);
    return kNoNode;
}

std::uint32_t PixelMathParser::parsePower()
{
    const std::uint32_t base = parsePrimary();
    if (base == kNoNode)
    {
        return kNoNode;
    }
    if (kind() != PixelMathTokenKind::Caret)
    {
        return base;
    }
    const PixelMathToken op = current();
    if (!enter(op))
    {
        return kNoNode;
    }
    const DepthGuard guard(m_depth, m_maxDepth);
    advance();
    // Right-associative, and the exponent may carry its own sign: `2^-1` and `2^3^2` both mean what
    // they are read to mean, and `2*3^2` is eighteen.
    const std::uint32_t exponent = parseUnary();
    if (exponent == kNoNode)
    {
        return kNoNode;
    }
    const std::array<std::uint32_t, 2u> children{base, exponent};
    PixelMathNode node;
    node.kind = PixelMathSyntaxKind::Binary;
    node.binary = PixelMathBinaryOperator::Power;
    node.span = op.span;
    return addNodeWithChildren(node, std::span<const std::uint32_t>(children));
}

std::uint32_t PixelMathParser::parseUnary()
{
    const PixelMathToken token = current();
    PixelMathUnaryOperator op = PixelMathUnaryOperator::Plus;
    switch (token.kind)
    {
    case PixelMathTokenKind::Plus:
        op = PixelMathUnaryOperator::Plus;
        break;
    case PixelMathTokenKind::Minus:
        op = PixelMathUnaryOperator::Negate;
        break;
    case PixelMathTokenKind::Tilde:
        op = PixelMathUnaryOperator::Complement;
        break;
    case PixelMathTokenKind::Bang:
        op = PixelMathUnaryOperator::Not;
        break;
    default:
        return parsePower();
    }
    if (!enter(token))
    {
        return kNoNode;
    }
    const DepthGuard guard(m_depth, m_maxDepth);
    advance();
    const std::uint32_t operand = parseUnary();
    if (operand == kNoNode)
    {
        return kNoNode;
    }
    const std::array<std::uint32_t, 1u> children{operand};
    PixelMathNode node;
    node.kind = PixelMathSyntaxKind::Unary;
    node.unary = op;
    node.span = token.span;
    return addNodeWithChildren(node, std::span<const std::uint32_t>(children));
}

std::uint32_t PixelMathParser::parseMultiplicative()
{
    std::uint32_t left = parseUnary();
    while (left != kNoNode)
    {
        PixelMathBinaryOperator op = PixelMathBinaryOperator::Multiply;
        switch (kind())
        {
        case PixelMathTokenKind::Star:
            op = PixelMathBinaryOperator::Multiply;
            break;
        case PixelMathTokenKind::Slash:
            op = PixelMathBinaryOperator::Divide;
            break;
        case PixelMathTokenKind::Percent:
            op = PixelMathBinaryOperator::Modulus;
            break;
        default:
            return left;
        }
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseUnary();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = op;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseAdditive()
{
    std::uint32_t left = parseMultiplicative();
    while (left != kNoNode)
    {
        PixelMathBinaryOperator op = PixelMathBinaryOperator::Add;
        switch (kind())
        {
        case PixelMathTokenKind::Plus:
            op = PixelMathBinaryOperator::Add;
            break;
        case PixelMathTokenKind::Minus:
            op = PixelMathBinaryOperator::Subtract;
            break;
        case PixelMathTokenKind::MinusMinus:
            op = PixelMathBinaryOperator::AbsoluteDifference;
            break;
        default:
            return left;
        }
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseMultiplicative();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = op;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseComparison()
{
    std::uint32_t left = parseAdditive();
    while (left != kNoNode)
    {
        PixelMathBinaryOperator op = PixelMathBinaryOperator::Less;
        switch (kind())
        {
        case PixelMathTokenKind::Less:
            op = PixelMathBinaryOperator::Less;
            break;
        case PixelMathTokenKind::LessEqual:
            op = PixelMathBinaryOperator::LessOrEqual;
            break;
        case PixelMathTokenKind::Greater:
            op = PixelMathBinaryOperator::Greater;
            break;
        case PixelMathTokenKind::GreaterEqual:
            op = PixelMathBinaryOperator::GreaterOrEqual;
            break;
        default:
            return left;
        }
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseAdditive();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = op;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseEquality()
{
    std::uint32_t left = parseComparison();
    while (left != kNoNode)
    {
        PixelMathBinaryOperator op = PixelMathBinaryOperator::Equal;
        if (kind() == PixelMathTokenKind::EqualEqual)
        {
            op = PixelMathBinaryOperator::Equal;
        }
        else if (kind() == PixelMathTokenKind::BangEqual)
        {
            op = PixelMathBinaryOperator::NotEqual;
        }
        else
        {
            return left;
        }
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseComparison();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = op;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseLogicalAnd()
{
    std::uint32_t left = parseEquality();
    while (left != kNoNode && kind() == PixelMathTokenKind::AmpersandAmpersand)
    {
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseEquality();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = PixelMathBinaryOperator::And;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseLogicalOr()
{
    std::uint32_t left = parseLogicalAnd();
    while (left != kNoNode && kind() == PixelMathTokenKind::PipePipe)
    {
        const PixelMathToken token = current();
        if (!enter(token))
        {
            return kNoNode;
        }
        const DepthGuard guard(m_depth, m_maxDepth);
        advance();
        const std::uint32_t right = parseLogicalAnd();
        if (right == kNoNode)
        {
            return kNoNode;
        }
        const std::array<std::uint32_t, 2u> children{left, right};
        PixelMathNode node;
        node.kind = PixelMathSyntaxKind::Binary;
        node.binary = PixelMathBinaryOperator::Or;
        node.span = token.span;
        left = addNodeWithChildren(node, std::span<const std::uint32_t>(children));
    }
    return left;
}

std::uint32_t PixelMathParser::parseAssignment()
{
    const std::uint32_t left = parseLogicalOr();
    if (left == kNoNode)
    {
        return kNoNode;
    }
    PixelMathAssignmentOperator op = PixelMathAssignmentOperator::Assign;
    switch (kind())
    {
    case PixelMathTokenKind::Assign:
        op = PixelMathAssignmentOperator::Assign;
        break;
    case PixelMathTokenKind::PlusAssign:
        op = PixelMathAssignmentOperator::AddAssign;
        break;
    case PixelMathTokenKind::MinusAssign:
        op = PixelMathAssignmentOperator::SubtractAssign;
        break;
    case PixelMathTokenKind::MinusMinusAssign:
        op = PixelMathAssignmentOperator::AbsoluteDifferenceAssign;
        break;
    case PixelMathTokenKind::StarAssign:
        op = PixelMathAssignmentOperator::MultiplyAssign;
        break;
    case PixelMathTokenKind::SlashAssign:
        op = PixelMathAssignmentOperator::DivideAssign;
        break;
    case PixelMathTokenKind::PercentAssign:
        op = PixelMathAssignmentOperator::ModulusAssign;
        break;
    case PixelMathTokenKind::CaretAssign:
        op = PixelMathAssignmentOperator::PowerAssign;
        break;
    default:
        return left;
    }

    const PixelMathToken token = current();
    if ((*m_tree.m_nodes)[left].kind != PixelMathSyntaxKind::Name)
    {
        refuseAt(PixelMathDiagnosticCode::AssignmentTargetNotVariable, (*m_tree.m_nodes)[left].span,
                 std::string(m_tree.name((*m_tree.m_nodes)[left].nameIndex)));
        return kNoNode;
    }
    if (!enter(token))
    {
        return kNoNode;
    }
    const DepthGuard guard(m_depth, m_maxDepth);
    advance();
    const std::uint32_t right = parseAssignment();
    if (right == kNoNode)
    {
        return kNoNode;
    }
    const std::array<std::uint32_t, 2u> children{left, right};
    PixelMathNode node;
    node.kind = PixelMathSyntaxKind::Assignment;
    node.assignment = op;
    node.span = token.span;
    return addNodeWithChildren(node, std::span<const std::uint32_t>(children));
}

std::uint32_t PixelMathParser::parseStatements(bool insideParentheses)
{
    std::vector<std::uint32_t> statements;
    while (true)
    {
        const std::size_t startedAt = m_position;
        const std::uint32_t statement = parseAssignment();
        if (statement == kNoNode)
        {
            if (insideParentheses)
            {
                return kNoNode;
            }
            recover();
            if (m_position == startedAt)
            {
                // Nothing was consumed - a closing bracket with no opening one is the shape that
                // does this - so one token is skipped by force. A recovery that can stand still is
                // a recovery that can loop forever.
                advance();
            }
            if (kind() == PixelMathTokenKind::End)
            {
                break;
            }
            m_statementFailed = false;
            continue;
        }
        if (statements.size() >= m_limits.maxStatements)
        {
            refuseAt(PixelMathDiagnosticCode::TooManyStatements, current().span, {}, m_limits.maxStatements,
                     static_cast<std::uint32_t>(statements.size() + 1u));
            return kNoNode;
        }
        statements.push_back(statement);
        if (kind() != PixelMathTokenKind::Semicolon)
        {
            break;
        }
        advance();
        // A trailing separator is written by people and means nothing more than the statement before
        // it, so it ends the sequence rather than starting an empty one.
        if (kind() == PixelMathTokenKind::End || (insideParentheses && kind() == PixelMathTokenKind::RightParenthesis))
        {
            break;
        }
    }

    if (statements.empty())
    {
        return kNoNode;
    }
    if (!insideParentheses)
    {
        m_tree.m_statementCount = static_cast<std::uint32_t>(statements.size());
    }
    if (statements.size() == 1u)
    {
        return statements.front();
    }
    PixelMathNode node;
    node.kind = PixelMathSyntaxKind::Sequence;
    node.span = PixelMathSourceSpan{(*m_tree.m_nodes)[statements.front()].span.offset, 0u};
    return addNodeWithChildren(node, std::span<const std::uint32_t>(statements));
}

PixelMathParse PixelMathParser::run()
{
    if (m_source.size() > m_limits.maxSourceBytes)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::SourceTooLong;
        diagnostic.expected = m_limits.maxSourceBytes;
        diagnostic.actual = m_source.size() > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<std::uint32_t>(m_source.size());
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
        return std::move(m_parse);
    }

    m_tokens = lexPixelMath(m_source);
    for (const PixelMathDiagnostic& lexical : m_tokens.diagnostics)
    {
        m_parse.m_diagnostics->push_back(lexical);
    }
    if (m_tokens.tokens.size() == 1u && m_parse.m_diagnostics->empty())
    {
        // Nothing but whitespace and comments. Said as what it is rather than as a token that was
        // not there, because "the source holds no expression" is what the person has to fix.
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::EmptyExpression;
        diagnostic.span = PixelMathSourceSpan{0u, static_cast<std::uint32_t>(m_source.size())};
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
        return std::move(m_parse);
    }

    const std::uint32_t root = parseStatements(false);
    if (root == kNoNode || !m_parse.m_diagnostics->empty())
    {
        if (m_parse.m_diagnostics->empty())
        {
            PixelMathDiagnostic diagnostic;
            diagnostic.code = PixelMathDiagnosticCode::EmptyExpression;
            diagnostic.span = PixelMathSourceSpan{0u, static_cast<std::uint32_t>(m_source.size())};
            m_parse.m_diagnostics->push_back(std::move(diagnostic));
        }
        return std::move(m_parse);
    }
    if (kind() != PixelMathTokenKind::End)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::UnexpectedToken;
        diagnostic.span = current().span;
        diagnostic.name = std::string(current().text);
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
        return std::move(m_parse);
    }
    if (m_tree.m_names->size() > m_limits.maxNames)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::TooManyNames;
        diagnostic.expected = m_limits.maxNames;
        diagnostic.actual = static_cast<std::uint32_t>(m_tree.m_names->size());
        m_parse.m_diagnostics->push_back(std::move(diagnostic));
        return std::move(m_parse);
    }

    m_tree.m_rootIndex = root;
    m_tree.m_maxDepth = m_maxDepth + 1u;
    m_parse.m_tree = std::move(m_tree);
    return std::move(m_parse);
}

Status PixelMathParse::status() const
{
    if (m_allocationFailed)
        return Status::failure(ErrorCode::AllocationFailed);
    if (!m_diagnostics || m_diagnostics->empty())
    {
        return Status::success();
    }
    return pixelMathDiagnosticStatus(m_diagnostics->front());
}

PixelMathParse parsePixelMath(std::string_view source, const PixelMathParseLimits& limits)
{
    try
    {
        PixelMathParser parser(source, limits);
        return parser.run();
    }
    catch (const std::bad_alloc&)
    {
        return PixelMathParser::allocationFailure();
    }
}

} // namespace an
