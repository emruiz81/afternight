// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What must hold of every byte string offered to the expression front end.
//
// The parser is the one piece of this engine whose input is written by a person, which makes it the
// one piece whose input is arbitrary by design. It already carries a committed adversarial corpus and
// a fifty-thousand-mutation sweep inside the ordinary suite; this target is the same subject under
// the campaign's own budget, seeded from the same corpus so that the two cannot drift.
//
// The claims are about the shape of what comes back rather than about what any one expression means:
//
// * **A parse is a tree or a reason, never both and never neither.**
// * **A tree is well formed.** Every child was built before its parent, every node's kind is one this
//   build knows, and every name index is inside the name table - the three properties an index-based
//   syntax tree can violate without crashing, and the three that would make a later pass read the
//   wrong node.
// * **Every diagnostic points inside the text it is about.** A span past the end of the source is how
//   a diagnostic printed next to its input turns into an out-of-bounds read in whatever prints it.
// * **Its own limits are obeyed.** A parse that succeeded did so within the depth, node, statement
//   and name bounds it was given, which is what stops a deeply nested expression becoming a stack
//   overflow in a later recursive pass.
//
// The limits are fuzzed too, because a parser that consulted the defaults rather than the limits it
// was handed would pass every test written with the defaults.

#include "../FuzzEngine/FuzzHarness.h"
#include "../FuzzEngine/PixelMathCorpus.h"

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <optional>
#include <string>
#include <vector>

namespace an::fuzz
{

TargetInfo targetInfo()
{
    // The parser's own default source bound is 65,536 bytes; giving the target a little more means a
    // mutation can reach the refusal rather than only the acceptance.
    return {.name = "Fuzz_EnginePixelMath", .maxInputBytes = 70000u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> corpus;
    for (const std::string_view source : kPixelMathAdversarialCorpus)
    {
        corpus.emplace_back(reinterpret_cast<const std::uint8_t*>(source.data()),
                            reinterpret_cast<const std::uint8_t*>(source.data()) + source.size());
    }
    // Expressions that parse, so the mutator has something whole to break rather than only things
    // that are already broken. A corpus of refusals alone never reaches resolution at all.
    static constexpr const char* kValid[] = {
        "$T",
        "$T * 0.5 + 0.25",
        "iif($T > 0.5, 1, 0)",
        "med($T) + mdev($T)",
        "pixel($T, 1, 2)",
        "$T[0] * $T[1] - $T[2]",
        "k = 3; $T * k",
        "sqrt(abs($T)) ^ 2.2",
        "atan2($T, 1) + log(max($T, 1e-6))",
    };
    for (const char* source : kValid)
    {
        const std::string text(source);
        corpus.emplace_back(reinterpret_cast<const std::uint8_t*>(text.data()),
                            reinterpret_cast<const std::uint8_t*>(text.data()) + text.size());
    }
    return corpus;
}

} // namespace an::fuzz

namespace
{

/// What the parser is allowed to build for this input.
///
/// @param input where the choices come from, read from the back so the expression stays whole.
/// @return the limits.
[[nodiscard]] an::PixelMathParseLimits limitsFrom(an::fuzz::Input& input)
{
    an::PixelMathParseLimits limits;
    switch (input.controlBelow(5u))
    {
    case 0:
        break;
    case 1:
        limits.maxDepth = 1u + input.controlBelow(8u);
        break;
    case 2:
        limits.maxNodes = 1u + input.controlBelow(64u);
        break;
    case 3:
        limits.maxStatements = 1u + input.controlBelow(4u);
        break;
    default:
        limits.maxSourceBytes = 1u + input.controlBelow(255u);
        limits.maxNames = 1u + input.controlBelow(8u);
        break;
    }
    return limits;
}

/// The images and constants a name may resolve to.
///
/// Built once: what this target varies is the expression, and a resolution that depended on the
/// order bindings were made in would be a defect of its own rather than something to fuzz here.
///
/// @return the bindings, which name one target image of three channels and one constant.
[[nodiscard]] const an::PixelMathBindings& bindings()
{
    static const an::PixelMathBindings built = []() {
        an::PixelMathBindings made;
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8u, 4u, 3u, an::defaultEngineLimits());
        AN_FUZZ_CHECK(extent.has_value(), "the bound image's extent must exist");
        const an::PixelMathImageDeclaration declaration{*extent, an::SampleType::Float32,
                                                        an::NumericDomain::NormalizedLinear};
        AN_FUZZ_CHECK(made.bindImage("T", declaration).ok(), "the target image must bind");
        AN_FUZZ_CHECK(made.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok(), "the target symbol must bind");
        AN_FUZZ_CHECK(made.declareConstant("k", 0.5).ok(), "the constant must declare");
        return made;
    }();
    return built;
}

/// Every claim a parse makes about itself.
///
/// @param source the text that was parsed.
/// @param limits what the parser was allowed to build.
/// @param parse what came back.
void checkParse(std::string_view source, const an::PixelMathParseLimits& limits, const an::PixelMathParse& parse)
{
    AN_FUZZ_CHECK(parse.succeeded() == parse.status().ok(),
                  "a parse that succeeded reports success and one that did not reports a failure");
    if (!parse.succeeded())
    {
        AN_FUZZ_CHECK(!parse.diagnostics().empty(), "a refused parse must say why");
        for (const an::PixelMathDiagnostic& diagnostic : parse.diagnostics())
        {
            AN_FUZZ_CHECK(diagnostic.code != an::PixelMathDiagnosticCode::None,
                          "a diagnostic must name something that went wrong");
            AN_FUZZ_CHECK(static_cast<std::size_t>(diagnostic.span.offset) <= source.size(),
                          "a diagnostic's span must start inside the text it is about");
            AN_FUZZ_CHECK(static_cast<std::size_t>(diagnostic.span.offset) +
                                  static_cast<std::size_t>(diagnostic.span.length) <=
                              source.size() + 1u,
                          "a diagnostic's span must end inside the text it is about");
        }
        return;
    }

    const an::PixelMathSyntaxTree& tree = parse.tree();
    AN_FUZZ_CHECK(!tree.empty(), "a successful parse builds a tree");
    AN_FUZZ_CHECK(tree.rootIndex() < tree.nodes().size(), "the root must be a node of the tree");
    // One more than the limit, and the difference is a contract rather than a slack: the limit counts
    // nesting steps and the report counts levels, so a leaf-only tree parsed under a limit of one
    // reports a depth of one and a tree that took that one step reports two. Both headers say so; this
    // is the claim that found out they did not.
    AN_FUZZ_CHECK(tree.maxDepth() >= 1u && tree.maxDepth() <= limits.maxDepth + 1u,
                  "the tree must respect its depth bound");
    AN_FUZZ_CHECK(tree.nodes().size() <= limits.maxNodes, "the tree must respect its node bound");
    AN_FUZZ_CHECK(tree.statementCount() >= 1u && tree.statementCount() <= limits.maxStatements,
                  "the tree must respect its statement bound");
    AN_FUZZ_CHECK(tree.nameCount() <= limits.maxNames, "the tree must respect its name bound");

    for (std::uint32_t index = 0u; index < tree.nodes().size(); ++index)
    {
        for (const std::uint32_t child : tree.children(index))
        {
            AN_FUZZ_CHECK(child < index, "a child must have been built before its parent");
        }
        const an::PixelMathNode& node = tree.nodes()[index];
        AN_FUZZ_CHECK(an::isPixelMathSyntaxKind(node.kind), "every node's kind must be one this build knows");
        if (node.kind == an::PixelMathSyntaxKind::Name || node.kind == an::PixelMathSyntaxKind::Call ||
            node.kind == an::PixelMathSyntaxKind::MetaSymbol)
        {
            AN_FUZZ_CHECK(node.nameIndex < tree.nameCount(), "a named node's name must be in the name table");
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    const an::PixelMathParseLimits limits = limitsFrom(input);
    const std::string source = input.restAsText();

    const an::PixelMathParse parsed = an::parsePixelMath(source, limits);
    checkParse(source, limits, parsed);
    an::fuzz::countReached(parsed.succeeded() ? "parsed" : "refused");
    if (!parsed.succeeded())
    {
        return 0;
    }

    const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings());
    AN_FUZZ_CHECK(resolved.succeeded() == resolved.status().ok(),
                  "a resolution that succeeded reports success and one that did not reports a failure");
    AN_FUZZ_CHECK(resolved.succeeded() || !resolved.diagnostics().empty(), "a refused resolution must say why");
    an::fuzz::countReached(resolved.succeeded() ? "resolved" : "unresolved");
    if (!resolved.succeeded())
    {
        return 0;
    }
    for (const std::uint32_t slot : resolved.imageSlots())
    {
        AN_FUZZ_CHECK(slot < bindings().imageCount(), "a resolved image slot must name a bound image");
    }
    return 0;
}
