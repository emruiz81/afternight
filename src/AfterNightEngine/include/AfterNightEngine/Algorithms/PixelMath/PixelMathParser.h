// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning an expression's text into its shape, under stated limits.
///
/// This entry point consumes untrusted input. An expression arrives from a user, a preset, a project
/// file or a script, and none of those is a promise about depth, length or balance. So every bound
/// the parse has is a number in one record rather than an implicit property of the machine it runs
/// on: how long the source may be, how many nodes it may build, how deep it may nest, how many
/// arguments a call may take, and how many statements one script may hold. A limit that is a number
/// can be tested at its boundary; a limit that is the call stack can only be found by exceeding it.
///
/// Nesting is checked on the way down rather than on the way back up. A thousand open parentheses
/// must cost a thousand comparisons and a refusal, not a thousand stack frames and then a refusal
/// that never arrives.
///
/// The parse reports every problem it finds, not the first, because the alternative is a user fixing
/// one typo per attempt. What it does not do is report problems caused by earlier ones: after a
/// refusal it skips to the next statement boundary before it starts believing the input again, so
/// one missing bracket is one diagnostic.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace an
{

/// What one parse is allowed to cost.
///
/// A plain aggregate whose defaults are the values this engine ships with. They are generous for
/// anything a person writes and small enough that the worst case is bounded: the defaults admit
/// sixty-four kibibytes of source, and refuse an expression designed to exhaust memory or the stack.
struct PixelMathParseLimits
{
    /// How many bytes of source will be read. Checked before the first character is looked at.
    std::uint32_t maxSourceBytes = 65536u;

    /// How many syntax nodes one expression may build.
    std::uint32_t maxNodes = 4096u;

    /// How deep one expression may nest. Checked while descending.
    ///
    /// This counts nesting *steps*, while `PixelMathSyntaxTree::maxDepth` counts *levels*, so a tree
    /// built under this limit reports a depth of at most one more than it. A leaf-only expression
    /// takes no step and reports depth one; `a + b` takes one step and reports two. A consumer sizing
    /// a recursive walk from this number rather than from the tree it is walking is therefore short
    /// by one, which is why the tree reports its own depth.
    std::uint32_t maxDepth = 64u;

    /// How many arguments one call may take.
    std::uint32_t maxCallArguments = 64u;

    /// How many semicolon-separated statements one script may hold.
    std::uint32_t maxStatements = 256u;

    /// How many distinct names one expression may write. Checked once the whole source has parsed,
    /// and reported as `PixelMathDiagnosticCode::TooManyNames` with this limit and the name count
    /// as its expected and actual counts, because the diagnostic vocabulary has no code of its own
    /// for it.
    std::uint32_t maxNames = 256u;
};

/// The limits this engine parses under when a caller states none.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @return the default limits.
[[nodiscard]] constexpr PixelMathParseLimits defaultPixelMathParseLimits() noexcept
{
    return PixelMathParseLimits{};
}

/// What one parse produced: a tree, or the reasons there is not one.
///
/// Returned by value rather than as a `Result`, because a front end's answer is not one value or
/// one failure - it is every problem the source has. A caller that only wants the returned failure
/// asks for `status()`; a caller building an editor walks `diagnostics()` and underlines each span.
///
/// Copyable and movable. Reading it from several threads at once is safe because it cannot be
/// changed once returned.
class PixelMathParse
{
public:
    /// A parse that produced nothing and says nothing.
    ///
    /// The state a default-constructed value holds; a parse that actually ran always carries either
    /// a tree, at least one diagnostic, or an allocation failure reported by `status()`.
    PixelMathParse() = default;

    /// Copies a parse, including its tree and diagnostics.
    PixelMathParse(const PixelMathParse&) = default;

    /// Takes over a parse's tree and diagnostics.
    PixelMathParse(PixelMathParse&&) noexcept = default;

    /// Copies a parse over this one.
    ///
    /// @return this parse.
    PixelMathParse& operator=(const PixelMathParse&) = default;

    /// Takes over a parse's tree and diagnostics, leaving the other empty.
    ///
    /// @return this parse.
    PixelMathParse& operator=(PixelMathParse&&) noexcept = default;

    /// Releases the tree and diagnostics.
    ~PixelMathParse() = default;

    /// Whether the source parsed.
    ///
    /// @return true when no diagnostic or allocation failure was raised.
    [[nodiscard]] bool succeeded() const noexcept
    {
        return !m_allocationFailed && (!m_diagnostics || m_diagnostics->empty());
    }

    /// The parsed expression.
    ///
    /// Borrowed for as long as this parse lives. Empty when the parse was refused, so a caller that
    /// ignored `succeeded()` walks nothing rather than a partial tree.
    ///
    /// @return the tree.
    [[nodiscard]] const PixelMathSyntaxTree& tree() const noexcept
    {
        return m_tree;
    }

    /// Every reason the source was refused.
    ///
    /// The lexical diagnostics come first, in source order, then the grammar's, in the order the
    /// parse met them. So a lexical problem late in the source is listed before a syntax error
    /// early in it. Borrowed for as long as this parse lives.
    ///
    /// @return the diagnostics, empty on success or allocation failure; ask `status()` to distinguish them.
    [[nodiscard]] std::span<const PixelMathDiagnostic> diagnostics() const noexcept
    {
        return m_diagnostics ? std::span<const PixelMathDiagnostic>(*m_diagnostics)
                             : std::span<const PixelMathDiagnostic>{};
    }

    /// The returned failure this parse stands for.
    ///
    /// Built from the first diagnostic in `diagnostics()` order: the earliest lexical problem when
    /// there is one, and otherwise the first problem the grammar met.
    ///
    /// @return success when the source parsed, and otherwise whatever
    ///         `pixelMathDiagnosticStatus` makes of the first diagnostic. Heap exhaustion returns
    ///         `ErrorCode::AllocationFailed` without allocating a diagnostic or a status detail.
    [[nodiscard]] AFTERNIGHTENGINE_API Status status() const;

private:
    friend class PixelMathParser;

    PixelMathSyntaxTree m_tree;
    std::optional<std::vector<PixelMathDiagnostic>> m_diagnostics;
    bool m_allocationFailed = false;
};

/// Parses one expression.
///
/// The source is borrowed for the call and nothing in the returned parse points into it: names are
/// copied into the tree, so the tree outlives the text it was written in.
///
/// Input refusals carry diagnostics. Heap exhaustion carries an allocation-free
/// `ErrorCode::AllocationFailed` through the returned parse's `status()`. The parse allocates in proportion to what it
/// accepts, bounded by `limits`.
///
/// Thread-safe and reentrant: it holds no state between calls and reads nothing but its arguments.
///
/// @param source the expression's text. May be empty, which is refused as an empty expression.
/// @param limits what this parse is allowed to cost.
/// @return the tree, the diagnostics that refused it, or an allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API PixelMathParse
parsePixelMath(std::string_view source, const PixelMathParseLimits& limits = defaultPixelMathParseLimits());

} // namespace an
