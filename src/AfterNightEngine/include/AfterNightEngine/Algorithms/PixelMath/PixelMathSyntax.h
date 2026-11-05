// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shape of an expression, before anything knows what its names mean.
///
/// The tree this header describes is the parser's whole output and the resolver's whole input. It
/// says what was written and nothing about what it means: a name is a name, not an image; a call is
/// a call, not a function; a channel selector is a number, not a channel that exists. Keeping those
/// apart is what lets one expression be parsed once and resolved against several sets of bindings,
/// and it is what lets a syntax error be reported without a single image being bound.
///
/// The nodes live in one array and refer to each other by index, rather than being allocated one at a
/// time and linked by raw pointers that a hand-written recursive walk has to free. The arena removes
/// the question: the tree owns its nodes, copying it copies them, and destroying it destroys them,
/// with no traversal and no leak to leak.
///
/// Depth is bounded by the parser rather than by the machine. A tree that reached this type is one
/// whose depth was checked while it was being built, so every consumer may walk it recursively
/// knowing the bound it was built under.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Core/Api.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// The one thing that builds a tree. Defined in the engine's implementation.
///
/// Named here rather than published because a caller has no use for it: it exists so that the type
/// below has exactly one producer, and so that every tree a caller can hold is one whose limits were
/// checked while it was built.
class PixelMathParser;

/// What one syntax node is.
enum class PixelMathSyntaxKind : std::uint8_t
{
    /// A numeric literal. Its value is in `PixelMathNode::number`.
    Number = 0u,

    /// A name, with an optional channel selector. What it refers to is not decided here.
    Name = 1u,

    /// One of the three meta symbols, with an optional channel selector.
    MetaSymbol = 2u,

    /// A prefix operator applied to its one child.
    Unary = 3u,

    /// An infix operator applied to its two children, left then right.
    Binary = 4u,

    /// A named call and its argument children, in written order. The conditional is one of these:
    /// it is a function whose arguments are not all evaluated rather than a syntax of its own, so
    /// that adding a second such function needs no new node kind.
    Call = 5u,

    /// An assignment: the target name and the value, in that order.
    Assignment = 6u,

    /// Two or more statements evaluated left to right; the last one is the expression's value.
    Sequence = 7u
};

/// Whether a value names a syntax node kind this build produces.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to inspect.
/// @return true for every kind the parser builds.
[[nodiscard]] constexpr bool isPixelMathSyntaxKind(PixelMathSyntaxKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(PixelMathSyntaxKind::Sequence);
}

/// A prefix operator.
enum class PixelMathUnaryOperator : std::uint8_t
{
    /// `+x`, which is `x`. Kept as a node rather than folded away so the written form survives.
    Plus = 0u,

    /// `-x`.
    Negate = 1u,

    /// `~x`, which is `1 - x`: the complement of a normalized sample.
    Complement = 2u,

    /// `!x`, which is one when `x` is zero and zero otherwise.
    Not = 3u
};

/// Whether a value names a prefix operator this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param op the value to inspect.
/// @return true for every supported operator.
[[nodiscard]] constexpr bool isPixelMathUnaryOperator(PixelMathUnaryOperator op) noexcept
{
    return static_cast<std::uint8_t>(op) <= static_cast<std::uint8_t>(PixelMathUnaryOperator::Not);
}

/// An infix operator.
enum class PixelMathBinaryOperator : std::uint8_t
{
    /// `a + b`.
    Add = 0u,

    /// `a - b`.
    Subtract = 1u,

    /// `a -- b`, the absolute difference.
    AbsoluteDifference = 2u,

    /// `a * b`.
    Multiply = 3u,

    /// `a / b`, under IEC 60559 division.
    Divide = 4u,

    /// `a % b`, the floating remainder with the sign of `a`.
    Modulus = 5u,

    /// `a ^ b`. Binds tighter than multiplication and groups to the right.
    ///
    /// This is how every published expression language reads `^`: `2 * 3 ^ 2` is eighteen and
    /// `2 ^ 3 ^ 2` is five hundred and twelve.
    Power = 6u,

    /// `a == b`, one or zero.
    Equal = 7u,

    /// `a != b`, one or zero.
    NotEqual = 8u,

    /// `a < b`, one or zero.
    Less = 9u,

    /// `a <= b`, one or zero.
    LessOrEqual = 10u,

    /// `a > b`, one or zero.
    Greater = 11u,

    /// `a >= b`, one or zero.
    GreaterOrEqual = 12u,

    /// `a && b`, one when neither side is zero. Both sides are evaluated.
    And = 13u,

    /// `a || b`, one when either side is not zero. Both sides are evaluated.
    Or = 14u
};

/// Whether a value names an infix operator this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param op the value to inspect.
/// @return true for every supported operator.
[[nodiscard]] constexpr bool isPixelMathBinaryOperator(PixelMathBinaryOperator op) noexcept
{
    return static_cast<std::uint8_t>(op) <= static_cast<std::uint8_t>(PixelMathBinaryOperator::Or);
}

/// An assignment operator.
///
/// Every compound form is defined as the matching infix operator applied to the variable's current
/// value and the right-hand side, which is what makes the table below complete rather than a second
/// list of arithmetic that could drift from the first.
enum class PixelMathAssignmentOperator : std::uint8_t
{
    /// `v = x`.
    Assign = 0u,

    /// `v += x`.
    AddAssign = 1u,

    /// `v -= x`.
    SubtractAssign = 2u,

    /// `v --= x`, the absolute difference.
    AbsoluteDifferenceAssign = 3u,

    /// `v *= x`.
    MultiplyAssign = 4u,

    /// `v /= x`.
    DivideAssign = 5u,

    /// `v %= x`.
    ModulusAssign = 6u,

    /// `v ^= x`.
    PowerAssign = 7u
};

/// Whether a value names an assignment operator this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param op the value to inspect.
/// @return true for every supported operator.
[[nodiscard]] constexpr bool isPixelMathAssignmentOperator(PixelMathAssignmentOperator op) noexcept
{
    return static_cast<std::uint8_t>(op) <= static_cast<std::uint8_t>(PixelMathAssignmentOperator::PowerAssign);
}

/// The infix operator a compound assignment applies.
///
/// `Assign` has none; it is reported as `Add` and is never read, because a plain assignment does not
/// combine with the target's current value. Thread-safe, allocation-free, and usable in a constant
/// expression.
///
/// @param op the assignment operator.
/// @return the matching infix operator, or `Add` for a plain assignment.
[[nodiscard]] constexpr PixelMathBinaryOperator pixelMathAssignmentOperation(PixelMathAssignmentOperator op) noexcept
{
    switch (op)
    {
    case PixelMathAssignmentOperator::AddAssign:
        return PixelMathBinaryOperator::Add;
    case PixelMathAssignmentOperator::SubtractAssign:
        return PixelMathBinaryOperator::Subtract;
    case PixelMathAssignmentOperator::AbsoluteDifferenceAssign:
        return PixelMathBinaryOperator::AbsoluteDifference;
    case PixelMathAssignmentOperator::MultiplyAssign:
        return PixelMathBinaryOperator::Multiply;
    case PixelMathAssignmentOperator::DivideAssign:
        return PixelMathBinaryOperator::Divide;
    case PixelMathAssignmentOperator::ModulusAssign:
        return PixelMathBinaryOperator::Modulus;
    case PixelMathAssignmentOperator::PowerAssign:
        return PixelMathBinaryOperator::Power;
    case PixelMathAssignmentOperator::Assign:
        break;
    }
    return PixelMathBinaryOperator::Add;
}

/// One of the three images an expression may name without knowing its name.
enum class PixelMathMetaSymbol : std::uint8_t
{
    /// `$T`, the image being written.
    Target = 0u,

    /// `$A`, the image the caller made active.
    Active = 1u,

    /// `$M`, the image the caller is using as a mask.
    Mask = 2u
};

/// Whether a value names a meta symbol this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param symbol the value to inspect.
/// @return true for every supported meta symbol.
[[nodiscard]] constexpr bool isPixelMathMetaSymbol(PixelMathMetaSymbol symbol) noexcept
{
    return static_cast<std::uint8_t>(symbol) <= static_cast<std::uint8_t>(PixelMathMetaSymbol::Mask);
}

/// The meta symbol's written form.
///
/// Thread-safe, reentrant, and allocation-free. The returned pointer is a string literal that
/// outlives any caller.
///
/// @param symbol the meta symbol, including values this build does not know.
/// @return the two-character spelling, or "unknown" for a value outside the enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API const char* pixelMathMetaSymbolName(PixelMathMetaSymbol symbol) noexcept;

/// One node of a parsed expression.
///
/// Which members carry meaning is decided by `kind`, and every other member is held at the neutral
/// value shown here. Children are named by a run in the tree's child table rather than by pointer,
/// so a tree is one allocation's worth of nodes and one of children, copyable by value.
struct PixelMathNode
{
    /// What this node is.
    PixelMathSyntaxKind kind = PixelMathSyntaxKind::Number;

    /// A `Number`'s value. Zero elsewhere.
    double number = 0.0;

    /// A `Unary`'s operator. `Plus` elsewhere.
    PixelMathUnaryOperator unary = PixelMathUnaryOperator::Plus;

    /// A `Binary`'s operator. `Add` elsewhere.
    PixelMathBinaryOperator binary = PixelMathBinaryOperator::Add;

    /// An `Assignment`'s operator. `Assign` elsewhere.
    PixelMathAssignmentOperator assignment = PixelMathAssignmentOperator::Assign;

    /// A `MetaSymbol`'s symbol. `Target` elsewhere.
    PixelMathMetaSymbol metaSymbol = PixelMathMetaSymbol::Target;

    /// Which of the tree's names a `Name` or `Call` node wrote. Zero elsewhere, and zero is also a
    /// valid name index, so read it only for those kinds.
    std::uint32_t nameIndex = 0u;

    /// The channel a `Name` or `MetaSymbol` node selected. Zero when none was written.
    std::uint32_t channel = 0u;

    /// Whether a channel selector was written at all. False elsewhere.
    bool hasChannel = false;

    /// Where this node's children start in the tree's child table. Zero when it has none.
    std::uint32_t firstChild = 0u;

    /// How many children it has. Zero for a leaf.
    std::uint32_t childCount = 0u;

    /// The source this node was written at.
    PixelMathSourceSpan span{};
};

/// A parsed expression: its nodes, its children, and the names it wrote.
///
/// Produced only by the parser, which is what makes every tree of this type one whose depth, node
/// count and statement count were checked while it was built. Copyable and movable; reading it from
/// any number of threads at once is safe because it cannot be changed once built.
class PixelMathSyntaxTree
{
public:
    /// An empty tree, holding no nodes.
    ///
    /// The state a refused parse leaves behind, so a caller that ignores the diagnostics walks
    /// nothing rather than walking rubble.
    PixelMathSyntaxTree() = default;

    /// Copies a tree, including its nodes and names.
    PixelMathSyntaxTree(const PixelMathSyntaxTree&) = default;

    /// Takes over a tree's nodes and names.
    PixelMathSyntaxTree(PixelMathSyntaxTree&&) noexcept = default;

    /// Copies a tree over this one.
    ///
    /// @return this tree.
    PixelMathSyntaxTree& operator=(const PixelMathSyntaxTree&) = default;

    /// Takes over a tree's nodes and names, leaving the other empty.
    ///
    /// @return this tree.
    PixelMathSyntaxTree& operator=(PixelMathSyntaxTree&&) noexcept = default;

    /// Releases the nodes and names.
    ~PixelMathSyntaxTree() = default;

    /// Every node, in the order the parser built them.
    ///
    /// Borrowed for as long as this tree lives.
    ///
    /// @return the nodes, empty for a tree that holds no expression.
    [[nodiscard]] std::span<const PixelMathNode> nodes() const noexcept
    {
        return m_nodes ? std::span<const PixelMathNode>(*m_nodes) : std::span<const PixelMathNode>{};
    }

    /// Which node is the expression's value.
    ///
    /// @return the root's index, or zero for an empty tree.
    [[nodiscard]] std::uint32_t rootIndex() const noexcept
    {
        return m_rootIndex;
    }

    /// Whether this tree holds an expression.
    ///
    /// @return true when it has at least one node.
    [[nodiscard]] bool empty() const noexcept
    {
        return !m_nodes || m_nodes->empty();
    }

    /// One node's children, in written order.
    ///
    /// Borrowed for as long as this tree lives.
    ///
    /// @param nodeIndex which node, below `nodes().size()`.
    /// @return the child indices, or an empty span for a leaf or an index naming no node.
    [[nodiscard]] std::span<const std::uint32_t> children(std::uint32_t nodeIndex) const noexcept
    {
        if (!m_nodes || nodeIndex >= m_nodes->size())
        {
            return {};
        }
        const PixelMathNode& node = (*m_nodes)[nodeIndex];
        if (node.childCount == 0u || !m_children || node.firstChild + node.childCount > m_children->size())
        {
            return {};
        }
        return std::span<const std::uint32_t>(*m_children).subspan(node.firstChild, node.childCount);
    }

    /// The text of one of the tree's names.
    ///
    /// Borrowed for as long as this tree lives.
    ///
    /// @param nameIndex which name, below `nameCount()`.
    /// @return the name, or an empty view when the index names none.
    [[nodiscard]] std::string_view name(std::uint32_t nameIndex) const noexcept
    {
        if (!m_names || nameIndex >= m_names->size())
        {
            return {};
        }
        return (*m_names)[nameIndex];
    }

    /// How many distinct names the expression wrote.
    ///
    /// @return the name count.
    [[nodiscard]] std::uint32_t nameCount() const noexcept
    {
        return m_names ? static_cast<std::uint32_t>(m_names->size()) : 0u;
    }

    /// The deepest nesting the tree reached.
    ///
    /// A leaf-only tree is depth one. Reported so a consumer that walks recursively can size its
    /// own working state from the tree rather than from the limit the tree was parsed under - and
    /// the two are not the same number. `PixelMathParseLimits::maxDepth` bounds the nesting *steps*
    /// a parse may take, and this counts the *levels* those steps produced, which is one more. So a
    /// tree parsed under a limit of one reports one or two, never more, and sizing from the limit
    /// alone is short by one.
    ///
    /// @return the depth, or zero for an empty tree.
    [[nodiscard]] std::uint32_t maxDepth() const noexcept
    {
        return m_maxDepth;
    }

    /// How many top-level statements the expression holds.
    ///
    /// One for an expression with no semicolons outside parentheses; a parenthesized sequence is a
    /// single node inside the statement that holds it.
    ///
    /// @return the statement count, or zero for an empty tree.
    [[nodiscard]] std::uint32_t statementCount() const noexcept
    {
        return m_statementCount;
    }

private:
    friend class PixelMathParser;

    /// Disengaged storage lets an empty tree be returned even when the heap is exhausted.
    std::optional<std::vector<PixelMathNode>> m_nodes;
    std::optional<std::vector<std::uint32_t>> m_children;
    std::optional<std::vector<std::string>> m_names;
    std::uint32_t m_rootIndex = 0u;
    std::uint32_t m_maxDepth = 0u;
    std::uint32_t m_statementCount = 0u;
};

} // namespace an
