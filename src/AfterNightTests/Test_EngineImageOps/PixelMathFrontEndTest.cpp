// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the expression front end: the function registry, the parser and its limits,
// name resolution, and what every refusal says.

#include "../FuzzEngine/PixelMathCorpus.h"
#include "../HeapFailure.h"

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return *extent;
}

[[nodiscard]] an::PixelMathImageDeclaration
declarationOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
              an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    return an::PixelMathImageDeclaration{extentOf(width, height, channels), an::SampleType::Float32, domain};
}

/// Bindings with one three-channel target called `T`, plus whatever else a case adds.
[[nodiscard]] an::PixelMathBindings targetOnlyBindings(std::uint32_t channels = 3u)
{
    an::PixelMathBindings bindings;
    EXPECT_TRUE(bindings.bindImage("T", declarationOf(8u, 4u, channels)).ok());
    EXPECT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
    return bindings;
}

[[nodiscard]] an::PixelMathResolution resolve(std::string_view source, const an::PixelMathBindings& bindings)
{
    const an::PixelMathParse parsed = an::parsePixelMath(source);
    EXPECT_TRUE(parsed.succeeded()) << an::describePixelMathDiagnostic(parsed.diagnostics().front());
    return an::resolvePixelMath(parsed.tree(), bindings);
}

[[nodiscard]] an::PixelMathDiagnosticCode firstCode(std::span<const an::PixelMathDiagnostic> diagnostics)
{
    return diagnostics.empty() ? an::PixelMathDiagnosticCode::None : diagnostics.front().code;
}

/// The node the parse made its root, for the cases that check the shape rather than the value.
[[nodiscard]] const an::PixelMathNode& root(const an::PixelMathParse& parse)
{
    return parse.tree().nodes()[parse.tree().rootIndex()];
}

} // namespace

TEST(PixelMathFunctionRegistryTest, EveryNameIsFindableAndTheTableIsOrdered)
{
    const std::span<const an::PixelMathFunctionEntry> registry = an::pixelMathFunctionRegistry();
    ASSERT_FALSE(registry.empty());
    for (std::size_t index = 1u; index < registry.size(); ++index)
    {
        EXPECT_LT(registry[index - 1u].name, registry[index].name)
            << "the registry is binary-searched, so a row out of order makes a name unreachable";
    }
    for (const an::PixelMathFunctionEntry& entry : registry)
    {
        const an::PixelMathFunctionSignature* const found = an::findPixelMathFunction(entry.name);
        ASSERT_NE(found, nullptr) << entry.name;
        EXPECT_EQ(found->id, entry.signature.id);
        EXPECT_TRUE(an::isPixelMathFunctionClass(found->functionClass));
        EXPECT_TRUE(an::isPixelMathReservedName(entry.name));
    }

    const std::span<const an::PixelMathFunctionEntry> statistics = an::pixelMathImageStatisticRegistry();
    for (std::size_t index = 1u; index < statistics.size(); ++index)
    {
        EXPECT_LT(statistics[index - 1u].name, statistics[index].name);
    }
    for (const an::PixelMathFunctionEntry& entry : statistics)
    {
        ASSERT_NE(an::findPixelMathImageStatistic(entry.name), nullptr) << entry.name;
        EXPECT_EQ(entry.signature.functionClass, an::PixelMathFunctionClass::ImageStatistic);
    }

    EXPECT_EQ(an::findPixelMathFunction("nosuchfunction"), nullptr);
    EXPECT_FALSE(an::isPixelMathReservedName("myImage"));
    EXPECT_TRUE(an::isPixelMathReservedName("$T"));
}

TEST(PixelMathFunctionRegistryTest, EveryDocumentedFunctionNameIsAvailable)
{
    constexpr auto scalarAndContextNames = std::to_array<std::string_view>({
        "CIEL",   "CIEX",    "CIEY", "CIEZ", "CIEa",  "CIEb",    "CIEc",    "CIEh",   "HSLh",   "HSLl",    "HSLs",
        "HSVh",   "HSVs",    "HSVv", "X",    "Y",     "YCbCrCb", "YCbCrCr", "YCbCrY", "abs",    "acos",    "acosh",
        "asin",   "asinh",   "asqr", "atan", "atan2", "atanh",   "bwmv",    "c",      "ceil",   "channel", "channels",
        "clamp",  "cos",     "cosh", "e",    "exp",   "fac",     "floor",   "h",      "height", "iif",     "inellipse",
        "inrect", "iscolor", "ln",   "log",  "log10", "log2",    "mad",     "max",    "mdev",   "mean",    "med",
        "min",    "mtf",     "n",    "ncr",  "npr",   "p",       "pi",      "pixel",  "rand",   "random",  "range",
        "sdev",   "sign",    "sin",  "sinh", "sqrt",  "ssqr",    "sum",     "tan",    "tanh",   "trunc",   "var",
        "w",      "width",   "x",    "xpos", "y",     "ypos",
    });
    for (const std::string_view name : scalarAndContextNames)
    {
        EXPECT_NE(an::findPixelMathFunction(name), nullptr) << name;
    }

    constexpr auto imageStatisticNames = std::to_array<std::string_view>(
        {"adev", "asqr", "bwmv", "mad", "max", "mdev", "mean", "med", "min", "noise", "sdev", "ssqr", "var"});
    for (const std::string_view name : imageStatisticNames)
    {
        EXPECT_NE(an::findPixelMathImageStatistic(name), nullptr) << name;
    }
}

TEST(PixelMathFunctionRegistryTest, EveryComputationHasAStableNameAndTwoSpellingsShareOne)
{
    for (std::size_t index = 0u; index < an::kPixelMathFunctionCount; ++index)
    {
        const an::PixelMathFunctionId id = static_cast<an::PixelMathFunctionId>(index);
        EXPECT_STRNE(an::pixelMathFunctionIdName(id), "unknown") << index;
    }
    EXPECT_EQ(an::findPixelMathFunction("log")->id, an::findPixelMathFunction("log10")->id);
    EXPECT_EQ(an::findPixelMathFunction("clamp")->id, an::findPixelMathFunction("range")->id);
    EXPECT_EQ(an::findPixelMathFunction("p")->id, an::findPixelMathFunction("pixel")->id);
    EXPECT_EQ(an::findPixelMathFunction("rand")->id, an::findPixelMathFunction("random")->id);
    EXPECT_NE(an::findPixelMathFunction("x")->id, an::findPixelMathFunction("X")->id)
        << "this language is case sensitive and the two mean different things";
}

TEST(PixelMathParserTest, PowerBindsTighterThanMultiplicationAndGroupsToTheRight)
{
    // Power binds tighter than multiplication and groups to the right, rather than reading this as
    // thirty-six. Written as a tree rather than as a value here, because the value is the execution
    // suite's business.
    const an::PixelMathParse parsed = an::parsePixelMath("2 * 3 ^ 2");
    ASSERT_TRUE(parsed.succeeded());
    const an::PixelMathNode& top = root(parsed);
    ASSERT_EQ(top.kind, an::PixelMathSyntaxKind::Binary);
    EXPECT_EQ(top.binary, an::PixelMathBinaryOperator::Multiply);
    const std::span<const std::uint32_t> children = parsed.tree().children(parsed.tree().rootIndex());
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(parsed.tree().nodes()[children[1]].binary, an::PixelMathBinaryOperator::Power);

    const an::PixelMathParse chained = an::parsePixelMath("2 ^ 3 ^ 2");
    ASSERT_TRUE(chained.succeeded());
    const std::span<const std::uint32_t> right = chained.tree().children(chained.tree().rootIndex());
    ASSERT_EQ(right.size(), 2u);
    EXPECT_EQ(chained.tree().nodes()[right[1]].kind, an::PixelMathSyntaxKind::Binary)
        << "the right operand is the nested power, which is what right association means";
}

TEST(PixelMathParserTest, PrefixOperatorsNestAndTheDoubleMinusIsAnAbsoluteDifference)
{
    const an::PixelMathParse nested = an::parsePixelMath("- - 1");
    ASSERT_TRUE(nested.succeeded());
    ASSERT_EQ(root(nested).kind, an::PixelMathSyntaxKind::Unary);
    const std::span<const std::uint32_t> inner = nested.tree().children(nested.tree().rootIndex());
    ASSERT_EQ(inner.size(), 1u);
    EXPECT_EQ(nested.tree().nodes()[inner[0]].kind, an::PixelMathSyntaxKind::Unary);

    const an::PixelMathParse difference = an::parsePixelMath("3--1");
    ASSERT_TRUE(difference.succeeded());
    ASSERT_EQ(root(difference).kind, an::PixelMathSyntaxKind::Binary);
    EXPECT_EQ(root(difference).binary, an::PixelMathBinaryOperator::AbsoluteDifference);
}

TEST(PixelMathParserTest, CommentsAreSkippedAndAnUnclosedOneIsRefused)
{
    const an::PixelMathParse commented = an::parsePixelMath("1 /* two */ + /* three */ 2 // and a tail\n");
    ASSERT_TRUE(commented.succeeded());
    EXPECT_EQ(root(commented).binary, an::PixelMathBinaryOperator::Add);

    const an::PixelMathParse unterminated = an::parsePixelMath("1 + /* never closed");
    ASSERT_FALSE(unterminated.succeeded());
    EXPECT_EQ(firstCode(unterminated.diagnostics()), an::PixelMathDiagnosticCode::UnterminatedComment);
}

TEST(PixelMathParserTest, NumbersAcceptTheSpellingsPeopleWrite)
{
    for (const std::string_view source : {"0.5", ".5", "1.", "1e3", "1E-3", "12"})
    {
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        EXPECT_TRUE(parsed.succeeded()) << source << ": " << an::describePixelMathDiagnostic(parsed.diagnostics()[0]);
    }
    const an::PixelMathParse malformed = an::parsePixelMath("1.2.3");
    EXPECT_FALSE(malformed.succeeded());
    const an::PixelMathParse enormous = an::parsePixelMath("1e999");
    ASSERT_FALSE(enormous.succeeded());
    EXPECT_EQ(firstCode(enormous.diagnostics()), an::PixelMathDiagnosticCode::NumberOutOfRange);
}

TEST(PixelMathParserTest, ChannelSelectorsAreRangeCheckedBeforeTheyAreNarrowed)
{
    for (const std::string_view source : {"$T[-1]", "$T[1.5]", "$T[1e21]"})
    {
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        ASSERT_FALSE(parsed.succeeded()) << source;
        EXPECT_EQ(firstCode(parsed.diagnostics()), an::PixelMathDiagnosticCode::MalformedChannelSelector) << source;
    }

    const an::PixelMathParse maximum = an::parsePixelMath("$T[65535]");
    ASSERT_TRUE(maximum.succeeded());
    EXPECT_TRUE(root(maximum).hasChannel);
    EXPECT_EQ(root(maximum).channel, 65535u);
}

TEST(PixelMathParserTest, StatementsSequenceAndATrailingSeparatorEndsThem)
{
    const an::PixelMathParse parsed = an::parsePixelMath("k = 1; k + 2;");
    ASSERT_TRUE(parsed.succeeded()) << an::describePixelMathDiagnostic(parsed.diagnostics()[0]);
    EXPECT_EQ(parsed.tree().statementCount(), 2u);
    EXPECT_EQ(root(parsed).kind, an::PixelMathSyntaxKind::Sequence);
}

TEST(PixelMathParserTest, DeepNestingIsRefusedRatherThanExhaustingTheStack)
{
    // Ten thousand open parentheses. The point of the case is that it returns at all.
    std::string source(10000u, '(');
    source += "1";
    source.append(10000u, ')');
    const an::PixelMathParse parsed = an::parsePixelMath(source);
    ASSERT_FALSE(parsed.succeeded());
    EXPECT_EQ(firstCode(parsed.diagnostics()), an::PixelMathDiagnosticCode::TooDeeplyNested);
    EXPECT_EQ(an::pixelMathDiagnosticStatus(parsed.diagnostics().front()).code(), an::ErrorCode::LimitExceeded);
}

TEST(PixelMathParserTest, TheConfiguredLimitsAreTheOnesThatRefuse)
{
    an::PixelMathParseLimits limits;
    limits.maxSourceBytes = 8u;
    const an::PixelMathParse tooLong = an::parsePixelMath("1 + 2 + 3 + 4", limits);
    ASSERT_FALSE(tooLong.succeeded());
    EXPECT_EQ(firstCode(tooLong.diagnostics()), an::PixelMathDiagnosticCode::SourceTooLong);

    an::PixelMathParseLimits fewNodes = an::defaultPixelMathParseLimits();
    fewNodes.maxNodes = 3u;
    const an::PixelMathParse tooMany = an::parsePixelMath("1 + 2 + 3 + 4", fewNodes);
    ASSERT_FALSE(tooMany.succeeded());
    EXPECT_EQ(firstCode(tooMany.diagnostics()), an::PixelMathDiagnosticCode::TooManyNodes);

    an::PixelMathParseLimits fewArguments = an::defaultPixelMathParseLimits();
    fewArguments.maxCallArguments = 2u;
    const an::PixelMathParse tooManyArguments = an::parsePixelMath("med(1, 2, 3)", fewArguments);
    ASSERT_FALSE(tooManyArguments.succeeded());
    EXPECT_EQ(firstCode(tooManyArguments.diagnostics()), an::PixelMathDiagnosticCode::TooManyArguments);
}

TEST(PixelMathParserTest, TheDepthLimitCountsStepsAndTheTreeReportsLevels)
{
    // Found by the campaign target, which claimed the tree's depth could not exceed the limit it was
    // parsed under and was told otherwise by `k = 3;` at a limit of one. The two numbers are one
    // apart by construction - the limit bounds the nesting a descent may do, the report counts the
    // levels that descent produced - and neither header said so. Pinned here because a consumer
    // sizing a recursive walk from the limit rather than from the tree is short by exactly this.
    an::PixelMathParseLimits shallow = an::defaultPixelMathParseLimits();
    shallow.maxDepth = 1u;

    const an::PixelMathParse leaf = an::parsePixelMath("3", shallow);
    ASSERT_TRUE(leaf.succeeded()) << an::describePixelMathDiagnostic(leaf.diagnostics()[0]);
    EXPECT_EQ(leaf.tree().maxDepth(), 1u) << "a leaf takes no step";

    const an::PixelMathParse oneStep = an::parsePixelMath("k = 3;", shallow);
    ASSERT_TRUE(oneStep.succeeded()) << an::describePixelMathDiagnostic(oneStep.diagnostics()[0]);
    EXPECT_EQ(oneStep.tree().maxDepth(), shallow.maxDepth + 1u) << "one step is two levels";

    const an::PixelMathParse twoSteps = an::parsePixelMath("k = 3 + 4;", shallow);
    EXPECT_FALSE(twoSteps.succeeded()) << "the second step is the one the limit refuses";
    EXPECT_EQ(firstCode(twoSteps.diagnostics()), an::PixelMathDiagnosticCode::TooDeeplyNested);

    // The relation is the bound, at every limit and not only at one.
    for (std::uint32_t depth = 1u; depth <= 6u; ++depth)
    {
        an::PixelMathParseLimits limits = an::defaultPixelMathParseLimits();
        limits.maxDepth = depth;
        const an::PixelMathParse parsed = an::parsePixelMath("1 + 2 * (3 - 4) / 5 ^ 6", limits);
        if (parsed.succeeded())
        {
            EXPECT_LE(parsed.tree().maxDepth(), limits.maxDepth + 1u) << "at limit " << depth;
            EXPECT_GE(parsed.tree().maxDepth(), 1u) << "at limit " << depth;
        }
    }
}

TEST(PixelMathParserTest, AnEmptySourceIsSaidToBeEmptyRatherThanUnexpected)
{
    for (const std::string_view source : {"", "   ", "// only a comment\n", "/* nothing */"})
    {
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        ASSERT_FALSE(parsed.succeeded()) << source;
        EXPECT_EQ(firstCode(parsed.diagnostics()), an::PixelMathDiagnosticCode::EmptyExpression) << source;
    }
}

TEST(PixelMathParserTest, OneStatementProducesOneDiagnosticAndTheNextIsStillRead)
{
    // The second statement is fine; the first is not. A parser that resynchronised badly would
    // report the rest of the source as well.
    const an::PixelMathParse parsed = an::parsePixelMath("1 + ; 2 + 3; )");
    ASSERT_FALSE(parsed.succeeded());
    EXPECT_EQ(parsed.diagnostics().size(), 2u) << "one for each statement that was actually wrong";
    for (const an::PixelMathDiagnostic& diagnostic : parsed.diagnostics())
    {
        EXPECT_LE(diagnostic.span.offset, 13u);
    }
}

TEST(PixelMathResolutionTest, BindingsRefuseAReservedOrRepeatedName)
{
    an::PixelMathBindings bindings;
    EXPECT_EQ(bindings.bindImage("sqrt", declarationOf(4u, 4u, 1u)).code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(bindings.bindImage("A", declarationOf(4u, 4u, 1u)).ok());
    EXPECT_EQ(bindings.bindImage("A", declarationOf(4u, 4u, 1u)).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bindings.declareConstant("A", 1.0).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bindings.declareConstant("k", std::numeric_limits<double>::quiet_NaN()).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "B").code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(bindings.check().code, an::PixelMathDiagnosticCode::NoTargetImage);
    EXPECT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "A").ok());
    EXPECT_EQ(bindings.check().code, an::PixelMathDiagnosticCode::None);
}

TEST(PixelMathResolutionTest, TheTargetIsAlwaysImageZeroEvenWhenTheExpressionDoesNotReadIt)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    const an::PixelMathResolution resolved = resolve("0.5", bindings);
    ASSERT_TRUE(resolved.succeeded());
    ASSERT_EQ(resolved.imageSlots().size(), 1u);
    EXPECT_EQ(resolved.images()[0].extent.channelCount(), 3u);
    EXPECT_TRUE(resolved.targetExtent().has_value());
}

TEST(PixelMathResolutionTest, AnUnknownNameAndAnUnknownFunctionAreDifferentAnswers)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    EXPECT_EQ(firstCode(resolve("nothing + 1", bindings).diagnostics()), an::PixelMathDiagnosticCode::UnknownName);
    EXPECT_EQ(firstCode(resolve("nothing(1)", bindings).diagnostics()), an::PixelMathDiagnosticCode::UnknownFunction);
}

TEST(PixelMathResolutionTest, AnArityMismatchCarriesBothCounts)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    const an::PixelMathResolution resolved = resolve("atan2(1)", bindings);
    ASSERT_FALSE(resolved.succeeded());
    const an::PixelMathDiagnostic& diagnostic = resolved.diagnostics().front();
    EXPECT_EQ(diagnostic.code, an::PixelMathDiagnosticCode::ArgumentCountMismatch);
    EXPECT_EQ(diagnostic.expected, 2u);
    EXPECT_EQ(diagnostic.actual, 1u);
    EXPECT_EQ(diagnostic.name, "atan2");
}

TEST(PixelMathResolutionTest, OneNameCanBeAnAggregateAndAWholeImageStatistic)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    const an::PixelMathResolution aggregate = resolve("med(1, 2, 3)", bindings);
    ASSERT_TRUE(aggregate.succeeded());
    EXPECT_TRUE(aggregate.statistics().empty());

    const an::PixelMathResolution image = resolve("med($T)", bindings);
    ASSERT_TRUE(image.succeeded()) << an::describePixelMathDiagnostic(image.diagnostics()[0]);
    ASSERT_EQ(image.statistics().size(), 1u);
    EXPECT_EQ(image.statistics()[0].statistic, an::PixelMathStatistic::Median);
    EXPECT_EQ(image.statistics()[0].image, 0u);

    // Asking for the same statistic twice is one request.
    const an::PixelMathResolution twice = resolve("med($T) + med($T)", bindings);
    ASSERT_TRUE(twice.succeeded());
    EXPECT_EQ(twice.statistics().size(), 1u);
}

TEST(PixelMathResolutionTest, EveryWholeImageEstimatorResolvesToOneSharedRequest)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    constexpr std::array names{"adev($T)", "asqr($T)", "ssqr($T)", "bwmv($T)", "noise($T)"};
    for (const char* const expression : names)
    {
        const an::PixelMathResolution resolved = resolve(expression, bindings);
        ASSERT_TRUE(resolved.succeeded()) << expression;
        EXPECT_EQ(resolved.statistics().size(), 1u) << expression;
    }
}

TEST(PixelMathResolutionTest, AParameterThatNeedsAnImageRefusesAnExpression)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    EXPECT_EQ(firstCode(resolve("med($T + 1)", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::ArgumentCountMismatch)
        << "med of one number is the aggregate form, which needs at least three";
    EXPECT_EQ(firstCode(resolve("w($T + 1)", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::ImageArgumentRequired);
    EXPECT_EQ(firstCode(resolve("adev(1, 2)", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::ImageArgumentRequired)
        << "adev exists only as a statistic of an image";
}

TEST(PixelMathResolutionTest, AChannelSelectorIsCheckedAgainstTheImageThatCarriesIt)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    const an::PixelMathResolution good = resolve("$T[2]", bindings);
    ASSERT_TRUE(good.succeeded());
    const an::PixelMathResolution bad = resolve("$T[3]", bindings);
    ASSERT_FALSE(bad.succeeded());
    EXPECT_EQ(bad.diagnostics().front().code, an::PixelMathDiagnosticCode::ChannelOutOfRange);
    EXPECT_EQ(bad.diagnostics().front().expected, 3u);
    EXPECT_EQ(bad.diagnostics().front().actual, 3u);
}

TEST(PixelMathResolutionTest, ImagesReadPerPixelHaveToLineUpWithWhatIsBeingWritten)
{
    an::PixelMathBindings bindings = targetOnlyBindings();
    ASSERT_TRUE(bindings.bindImage("SAME", declarationOf(8u, 4u, 3u)).ok());
    ASSERT_TRUE(bindings.bindImage("MONO", declarationOf(8u, 4u, 1u)).ok());
    ASSERT_TRUE(bindings.bindImage("SMALL", declarationOf(4u, 4u, 3u)).ok());

    EXPECT_TRUE(resolve("$T + SAME", bindings).succeeded());
    EXPECT_TRUE(resolve("$T * MONO", bindings).succeeded()) << "a single channel is broadcast";
    EXPECT_EQ(firstCode(resolve("$T + SMALL", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::MismatchedImageExtent);
    EXPECT_TRUE(resolve("$T + pixel(SMALL, 0, 0)", bindings).succeeded())
        << "the sampling function states its coordinates, so a differently sized image is fine";
    EXPECT_TRUE(resolve("w(SMALL)", bindings).succeeded()) << "geometry is not a sample";
}

TEST(PixelMathResolutionTest, TwoImagesThatMeanDifferentKindsOfNumberAreRefusedByName)
{
    an::PixelMathBindings bindings = targetOnlyBindings();
    ASSERT_TRUE(bindings.bindImage("RAW", declarationOf(8u, 4u, 3u, an::NumericDomain::RawSensorCode)).ok());
    EXPECT_EQ(firstCode(resolve("$T + RAW", bindings).diagnostics()), an::PixelMathDiagnosticCode::MixedNumericDomains);
    EXPECT_EQ(firstCode(resolve("$T + med(RAW)", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::MixedNumericDomains);
}

TEST(PixelMathResolutionTest, AColourComponentNeedsAnImageWithColourChannels)
{
    const an::PixelMathBindings colour = targetOnlyBindings(3u);
    const an::PixelMathResolution converted = resolve("CIEL($T)", colour);
    ASSERT_TRUE(converted.succeeded()) << an::describePixelMathDiagnostic(converted.diagnostics()[0]);
    EXPECT_TRUE(converted.usesColorComponent());

    const an::PixelMathBindings mono = targetOnlyBindings(1u);
    const an::PixelMathResolution refused = resolve("CIEL($T)", mono);
    ASSERT_FALSE(refused.succeeded());
    EXPECT_EQ(refused.diagnostics().front().code, an::PixelMathDiagnosticCode::ColorImageRequired);
}

TEST(PixelMathResolutionTest, OnlyADeclaredVariableCanBeAssignedTo)
{
    an::PixelMathBindings bindings = targetOnlyBindings();
    ASSERT_TRUE(bindings.declareVariable("v", 0.0).ok());
    ASSERT_TRUE(bindings.declareConstant("k", 2.0).ok());

    const an::PixelMathResolution assigned = resolve("v = $T * k; v", bindings);
    ASSERT_TRUE(assigned.succeeded()) << an::describePixelMathDiagnostic(assigned.diagnostics()[0]);
    EXPECT_TRUE(assigned.usesAssignment());
    ASSERT_EQ(assigned.variableInitialValues().size(), 1u);
    ASSERT_EQ(assigned.constants().size(), 1u);
    EXPECT_DOUBLE_EQ(assigned.constants()[0], 2.0);

    EXPECT_EQ(firstCode(resolve("k = 1", bindings).diagnostics()),
              an::PixelMathDiagnosticCode::AssignmentTargetNotVariable)
        << "a constant is a name, so it is the resolver that knows it cannot be written to";

    // A meta symbol is not a name at all, so the parser already knows this is not an assignment
    // target and says so without any bindings being consulted.
    const an::PixelMathParse metaSymbol = an::parsePixelMath("$T = 1");
    ASSERT_FALSE(metaSymbol.succeeded());
    EXPECT_EQ(firstCode(metaSymbol.diagnostics()), an::PixelMathDiagnosticCode::AssignmentTargetNotVariable);
}

TEST(PixelMathResolutionTest, AnUnboundMetaSymbolAndAnUnboundTargetAreDifferentAnswers)
{
    const an::PixelMathBindings bindings = targetOnlyBindings();
    EXPECT_EQ(firstCode(resolve("$A + 1", bindings).diagnostics()), an::PixelMathDiagnosticCode::UnboundMetaSymbol);

    an::PixelMathBindings noTarget;
    ASSERT_TRUE(noTarget.bindImage("A", declarationOf(4u, 4u, 1u)).ok());
    const an::PixelMathParse parsed = an::parsePixelMath("A");
    ASSERT_TRUE(parsed.succeeded());
    const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), noTarget);
    ASSERT_FALSE(resolved.succeeded());
    EXPECT_EQ(resolved.diagnostics().front().code, an::PixelMathDiagnosticCode::NoTargetImage);
}

TEST(PixelMathResolutionTest, WhatAnExpressionNeedsIsReportedBeforeItIsExecuted)
{
    an::PixelMathBindings bindings = targetOnlyBindings();
    ASSERT_TRUE(bindings.declareVariable("v", 0.0).ok());

    const an::PixelMathResolution plain = resolve("$T * 2", bindings);
    ASSERT_TRUE(plain.succeeded());
    EXPECT_FALSE(plain.usesPixelPosition());
    EXPECT_FALSE(plain.usesRandom());
    EXPECT_FALSE(plain.usesImageSample());
    EXPECT_FALSE(plain.usesAssignment());

    const an::PixelMathResolution busy = resolve("v = x() + random() + pixel($T, 0, 0); v", bindings);
    ASSERT_TRUE(busy.succeeded()) << an::describePixelMathDiagnostic(busy.diagnostics()[0]);
    EXPECT_TRUE(busy.usesPixelPosition());
    EXPECT_TRUE(busy.usesRandom());
    EXPECT_TRUE(busy.usesImageSample());
    EXPECT_TRUE(busy.usesAssignment());
}

TEST(PixelMathDiagnosticTest, EveryCodeHasANameASentenceAndAnErrorCode)
{
    for (std::size_t index = 0u; index < an::kPixelMathDiagnosticCodeCount; ++index)
    {
        an::PixelMathDiagnostic diagnostic;
        diagnostic.code = static_cast<an::PixelMathDiagnosticCode>(index);
        diagnostic.name = "thing";
        EXPECT_STRNE(an::pixelMathDiagnosticCodeName(diagnostic.code), "unknown") << index;
        EXPECT_FALSE(an::describePixelMathDiagnostic(diagnostic).empty()) << index;
        const an::Status status = an::pixelMathDiagnosticStatus(diagnostic);
        if (diagnostic.code == an::PixelMathDiagnosticCode::None)
        {
            EXPECT_TRUE(status.ok());
        }
        else
        {
            EXPECT_FALSE(status.ok()) << index;
            ASSERT_NE(status.detail(), nullptr) << index;
            EXPECT_FALSE(status.detail()->context.empty()) << index;
        }
    }
}

namespace
{

/// Inputs written to break a parser: unbalanced everything, operators with nothing to work on,
/// numbers that are not numbers, names that are almost meta symbols, and structures whose only
/// purpose is to be deep. The corpus is committed rather than generated, so a case that once
/// crashed stays in the record, and it is shared with the campaign target that seeds from it.
using an::fuzz::kPixelMathAdversarialCorpus;

/// A deterministic byte mutator. Not a substitute for a coverage-guided campaign; what it is for is
/// that a change to the parser has to survive fifty thousand inputs nobody wrote by hand, from a
/// seed that reproduces them exactly.
class Mutator
{
public:
    explicit Mutator(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    [[nodiscard]] std::uint64_t next() noexcept
    {
        m_state ^= m_state << 13u;
        m_state ^= m_state >> 7u;
        m_state ^= m_state << 17u;
        return m_state;
    }

    [[nodiscard]] std::string mutate(std::string_view source)
    {
        static constexpr std::string_view kAlphabet = "()[]{},;+-*/%^~!<>=&|$.0123456789abcxyzTAM \n";
        std::string result(source);
        const std::size_t edits = 1u + static_cast<std::size_t>(next() % 4u);
        for (std::size_t edit = 0u; edit < edits; ++edit)
        {
            const std::uint64_t choice = next() % 3u;
            const char character = kAlphabet[next() % kAlphabet.size()];
            if (result.empty() || choice == 0u)
            {
                result.insert(result.size() == 0u ? 0u : static_cast<std::size_t>(next() % (result.size() + 1u)), 1u,
                              character);
            }
            else if (choice == 1u)
            {
                result[static_cast<std::size_t>(next() % result.size())] = character;
            }
            else
            {
                result.erase(static_cast<std::size_t>(next() % result.size()), 1u);
            }
        }
        return result;
    }

private:
    std::uint64_t m_state;
};

/// What must hold of every parse, whatever it was given.
void checkParseInvariants(std::string_view source, const an::PixelMathParse& parse)
{
    if (parse.succeeded())
    {
        const an::PixelMathSyntaxTree& tree = parse.tree();
        ASSERT_FALSE(tree.empty()) << source;
        ASSERT_LT(tree.rootIndex(), tree.nodes().size()) << source;
        EXPECT_GE(tree.maxDepth(), 1u) << source;
        EXPECT_GE(tree.statementCount(), 1u) << source;
        for (std::uint32_t index = 0u; index < tree.nodes().size(); ++index)
        {
            for (const std::uint32_t child : tree.children(index))
            {
                EXPECT_LT(child, index) << source << ": a child must have been built before its parent";
            }
            const an::PixelMathNode& node = tree.nodes()[index];
            EXPECT_TRUE(an::isPixelMathSyntaxKind(node.kind)) << source;
            if (node.kind == an::PixelMathSyntaxKind::Name || node.kind == an::PixelMathSyntaxKind::Call ||
                node.kind == an::PixelMathSyntaxKind::MetaSymbol)
            {
                EXPECT_LT(node.nameIndex, tree.nameCount()) << source;
            }
        }
        return;
    }

    ASSERT_FALSE(parse.diagnostics().empty()) << source;
    for (const an::PixelMathDiagnostic& diagnostic : parse.diagnostics())
    {
        EXPECT_NE(diagnostic.code, an::PixelMathDiagnosticCode::None) << source;
        EXPECT_LE(static_cast<std::size_t>(diagnostic.span.offset), source.size())
            << source << ": a span has to be inside the source it is about";
        EXPECT_LE(static_cast<std::size_t>(diagnostic.span.offset) + diagnostic.span.length, source.size() + 1u)
            << source;
    }
    EXPECT_FALSE(parse.status().ok()) << source;
}

} // namespace

TEST(PixelMathFuzzTest, TheCommittedAdversarialCorpusIsAnsweredRatherThanCrashed)
{
    for (const std::string_view source : kPixelMathAdversarialCorpus)
    {
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        checkParseInvariants(source, parsed);
        if (parsed.succeeded())
        {
            const an::PixelMathBindings bindings = targetOnlyBindings();
            const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
            EXPECT_TRUE(resolved.succeeded() || !resolved.diagnostics().empty()) << source;
        }
    }
}

TEST(PixelMathFuzzTest, FiftyThousandMutationsOfItAreAnsweredToo)
{
    Mutator mutator(0x5EED1234u);
    const an::PixelMathBindings bindings = targetOnlyBindings();
    std::size_t accepted = 0u;
    std::size_t refused = 0u;
    for (std::size_t round = 0u; round < 50000u; ++round)
    {
        const std::string_view seed = kPixelMathAdversarialCorpus[round % kPixelMathAdversarialCorpus.size()];
        const std::string source = mutator.mutate(seed);
        const an::PixelMathParse parsed = an::parsePixelMath(source);
        ASSERT_NO_FATAL_FAILURE(checkParseInvariants(source, parsed)) << source;
        if (!parsed.succeeded())
        {
            ++refused;
            continue;
        }
        ++accepted;
        const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
        ASSERT_TRUE(resolved.succeeded() || !resolved.diagnostics().empty()) << source;
    }
    // Both numbers belong in the record: a sweep that accepted nothing and a sweep that accepted
    // everything print the same passing line otherwise.
    std::cout << "[          ] mutations accepted " << accepted << ", refused " << refused << std::endl;
    EXPECT_GT(accepted, 0u);
    EXPECT_GT(refused, 0u);
}

TEST(PixelMathParserTest, HeapExhaustionDuringConstructionReturnsAllocationFailure)
{
    if (!an::test::HeapFailure::kAvailable)
        GTEST_SKIP() << "requires the shared MSVC Debug CRT allocation hook";
    bool refused = false;
    bool empty = false;
    an::ErrorCode code = an::ErrorCode::Ok;
    {
        const an::test::HeapFailure failure(0u);
        const auto parsed = an::parsePixelMath("1 + 2");
        refused = failure.refused() != 0u;
        code = parsed.status().code();
        empty = parsed.tree().empty() && parsed.diagnostics().empty() && !parsed.succeeded();
    }
    EXPECT_TRUE(refused);
    EXPECT_EQ(code, an::ErrorCode::AllocationFailed);
    EXPECT_TRUE(empty);
    EXPECT_TRUE(an::parsePixelMath("1 + 2").succeeded());
}

TEST(PixelMathParserTest, NameLimitReportsNamesRatherThanNodes)
{
    auto limits = an::defaultPixelMathParseLimits();
    limits.maxNames = 1u;
    const auto parsed = an::parsePixelMath("alpha + beta", limits);
    ASSERT_FALSE(parsed.succeeded());
    ASSERT_EQ(parsed.diagnostics().size(), 1u);
    EXPECT_EQ(parsed.diagnostics()[0].code, an::PixelMathDiagnosticCode::TooManyNames);
    EXPECT_EQ(parsed.diagnostics()[0].expected, 1u);
    EXPECT_EQ(parsed.diagnostics()[0].actual, 2u);
}

TEST(PixelMathResolutionTest, TooManyArgumentsReportsTheMaximum)
{
    const auto result = resolve("p($T, 1, 2, 0, 0)", targetOnlyBindings());
    ASSERT_FALSE(result.succeeded());
    const auto& diagnostic = result.diagnostics().front();
    EXPECT_EQ(diagnostic.code, an::PixelMathDiagnosticCode::ArgumentCountMismatch);
    EXPECT_EQ(diagnostic.expected, 4u);
    EXPECT_EQ(diagnostic.actual, 5u);
}
