// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ValidityDescriptor.h.
//
// Three properties are what this type exists for, and each of them is a way to answer the question
// wrong.
//
// **The rule is carried rather than inferred.** Deciding per algorithm whether an exact zero is data
// makes two passes over one frame disagree about how many pixels it has. The cases below assert the
// default is all-valid and that nothing derives a rule from a sample.
//
// **Exact zero is never acquired.** `ZeroIsNoData` is a real convention and a catastrophic default,
// so the value cases check it against every representation and check that no other rule behaves like
// it - including over an image that happens to be full of zeroes.
//
// **A policy is declared and honoured, and refusing is a legitimate answer.** The policy cases are
// the preservation and combination half of the model: preserving two different claims is refused
// rather than resolved, combining two different claims answers with the mask rule, and an operation
// that has no meaning on incomplete data says so with a code its caller can act on.
//
// The empty-selection cases are the fourth property and the one that reaches furthest: a reduction
// over nothing is an outcome rather than a NaN, and it is the same outcome everywhere because there
// is one function that produces it.

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/DescriptorError.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace
{

/// Every rule this build knows, pinned so that adding one without deciding what it means here fails.
constexpr an::ValidityRule kAllRules[] = {an::ValidityRule::AllValid, an::ValidityRule::FiniteOnly,
                                          an::ValidityRule::ZeroIsNoData, an::ValidityRule::ExplicitMask,
                                          an::ValidityRule::DomainRange};

/// Every policy this build knows, pinned for the same reason.
constexpr an::ValidityPolicy kAllPolicies[] = {an::ValidityPolicy::Preserve, an::ValidityPolicy::Combine,
                                               an::ValidityPolicy::Regenerate, an::ValidityPolicy::Reject};

/// A description carrying one rule that needs no range.
///
/// @param rule the rule to carry.
/// @return the description, which is valid or the case has already failed.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    const std::optional<an::ValidityDescriptor> made =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
    EXPECT_TRUE(made.has_value());
    return made.value_or(an::ValidityDescriptor{});
}

/// A description carrying a closed range.
///
/// @param lowest the smallest value a sample may hold.
/// @param highest the largest.
/// @return the description, which is valid or the case has already failed.
[[nodiscard]] an::ValidityDescriptor rangeOf(double lowest, double highest)
{
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(an::ValidityDescriptorRequest{
        .rule = an::ValidityRule::DomainRange, .lowestValid = lowest, .highestValid = highest});
    EXPECT_TRUE(made.has_value());
    return made.value_or(an::ValidityDescriptor{});
}

} // namespace

TEST(ValidityRuleTest, TheCountCoversEveryEnumerator)
{
    EXPECT_EQ(an::kValidityRuleCount, sizeof(kAllRules) / sizeof(kAllRules[0]));
}

TEST(ValidityRuleTest, EveryRuleIsOneThisBuildKnowsAndAValueBeyondThemIsNot)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        EXPECT_TRUE(an::isValidityRule(rule));
    }
    EXPECT_FALSE(an::isValidityRule(static_cast<an::ValidityRule>(an::kValidityRuleCount)));
}

TEST(ValidityRuleTest, EveryRuleHasItsOwnName)
{
    for (const an::ValidityRule left : kAllRules)
    {
        for (const an::ValidityRule right : kAllRules)
        {
            const bool sameName = std::strcmp(an::validityRuleName(left), an::validityRuleName(right)) == 0;
            EXPECT_EQ(left == right, sameName);
        }
    }
    EXPECT_STREQ(an::validityRuleName(static_cast<an::ValidityRule>(an::kValidityRuleCount)), "UnknownValidityRule");
}

TEST(ValidityRuleTest, ExactlyOneRuleNeedsAMaskAndExactlyOneNeedsARange)
{
    std::size_t masked = 0u;
    std::size_t ranged = 0u;
    for (const an::ValidityRule rule : kAllRules)
    {
        masked += an::validityRuleNeedsMask(rule) ? 1u : 0u;
        ranged += an::validityRuleNeedsRange(rule) ? 1u : 0u;
    }
    EXPECT_EQ(masked, 1u);
    EXPECT_EQ(ranged, 1u);
    EXPECT_TRUE(an::validityRuleNeedsMask(an::ValidityRule::ExplicitMask));
    EXPECT_TRUE(an::validityRuleNeedsRange(an::ValidityRule::DomainRange));
}

TEST(ValidityRuleTest, EveryRuleButTheMaskCanBeDecidedFromAValue)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        EXPECT_EQ(rule != an::ValidityRule::ExplicitMask, an::validityRuleIsDecidedByValue(rule));
    }
    EXPECT_FALSE(an::validityRuleIsDecidedByValue(static_cast<an::ValidityRule>(an::kValidityRuleCount)));
}

TEST(ValidityDescriptorTest, TheDefaultClaimsEverySampleMeansSomething)
{
    constexpr an::ValidityDescriptor defaulted;
    static_assert(defaulted.rule() == an::ValidityRule::AllValid);
    static_assert(defaulted.isAllValid());
    static_assert(!defaulted.needsMask());
    static_assert(!defaulted.lowestValid().has_value());
    static_assert(!defaulted.highestValid().has_value());
    static_assert(defaulted == an::ValidityDescriptor::allValid());
}

TEST(ValidityDescriptorTest, ARuleOutsideTheEnumerationIsRefusedBeforeAnyRuleReadsIt)
{
    const an::ValidityDescriptorRequest request{
        .rule = static_cast<an::ValidityRule>(an::kValidityRuleCount), .lowestValid = 0.0, .highestValid = 1.0};
    EXPECT_EQ(an::ValidityDescriptor::check(request), an::DescriptorError::UnknownValidityRule);
    EXPECT_FALSE(an::ValidityDescriptor::create(request).has_value());
}

TEST(ValidityDescriptorTest, ARangeRuleWithoutARangeNamesNoSamples)
{
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange}),
              an::DescriptorError::ValidityRangeMissing);
    EXPECT_EQ(an::ValidityDescriptor::check(
                  an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0}),
              an::DescriptorError::ValidityRangeMissing);
    EXPECT_EQ(an::ValidityDescriptor::check(
                  an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange, .highestValid = 1.0}),
              an::DescriptorError::ValidityRangeMissing);
}

TEST(ValidityDescriptorTest, ARangeStatedForARuleThatDecidesOtherwiseIsAnUnsetFieldSomewhere)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        if (an::validityRuleNeedsRange(rule))
        {
            continue;
        }
        EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{.rule = rule, .lowestValid = 0.0}),
                  an::DescriptorError::ValidityRangeNotAllowed);
        EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{.rule = rule, .highestValid = 1.0}),
                  an::DescriptorError::ValidityRangeNotAllowed);
    }
}

TEST(ValidityDescriptorTest, ANonFiniteBoundIsNotABound)
{
    const double notANumber = std::numeric_limits<double>::quiet_NaN();
    const double infinite = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{
                  .rule = an::ValidityRule::DomainRange, .lowestValid = notANumber, .highestValid = 1.0}),
              an::DescriptorError::ValidityRangeNotFinite);
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{
                  .rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = infinite}),
              an::DescriptorError::ValidityRangeNotFinite);
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{
                  .rule = an::ValidityRule::DomainRange, .lowestValid = -infinite, .highestValid = 1.0}),
              an::DescriptorError::ValidityRangeNotFinite);
}

TEST(ValidityDescriptorTest, ARangeStatedTheWrongWayRoundIsRefusedRatherThanReadAsEmpty)
{
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{
                  .rule = an::ValidityRule::DomainRange, .lowestValid = 1.0, .highestValid = 0.0}),
              an::DescriptorError::ValidityRangeNotOrdered);
    // One value is a range of one sample and is legitimate.
    EXPECT_EQ(an::ValidityDescriptor::check(an::ValidityDescriptorRequest{
                  .rule = an::ValidityRule::DomainRange, .lowestValid = 1.0, .highestValid = 1.0}),
              an::DescriptorError::None);
}

TEST(ValidityDescriptorTest, ADescriptionRebuildsFromItsOwnRequest)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        const an::ValidityDescriptor described = an::validityRuleNeedsRange(rule) ? rangeOf(-2.5, 7.25) : ruleOf(rule);
        const std::optional<an::ValidityDescriptor> rebuilt = an::ValidityDescriptor::create(described.toRequest());
        ASSERT_TRUE(rebuilt.has_value());
        EXPECT_EQ(described, *rebuilt);
    }
}

TEST(ValidityDescriptorTest, OnlyTheMaskRuleNeedsAMaskAndOnlyAllValidClaimsEverything)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        const an::ValidityDescriptor described = an::validityRuleNeedsRange(rule) ? rangeOf(0.0, 1.0) : ruleOf(rule);
        EXPECT_EQ(rule == an::ValidityRule::ExplicitMask, described.needsMask());
        EXPECT_EQ(rule == an::ValidityRule::AllValid, described.isAllValid());
    }
}

TEST(ValidityValueTest, AllValidAndTheMaskRuleSayNothingAboutAValue)
{
    for (const an::ValidityRule rule : {an::ValidityRule::AllValid, an::ValidityRule::ExplicitMask})
    {
        const an::ValidityDescriptor described = ruleOf(rule);
        EXPECT_TRUE(an::sampleIsValidByValue<float>(described, 0.0f));
        EXPECT_TRUE(an::sampleIsValidByValue<float>(described, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(an::sampleIsValidByValue<std::uint16_t>(described, 0u));
    }
}

TEST(ValidityValueTest, FiniteOnlyRejectsExactlyTheThreeNonFiniteValues)
{
    const an::ValidityDescriptor described = ruleOf(an::ValidityRule::FiniteOnly);
    EXPECT_FALSE(an::sampleIsValidByValue<float>(described, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(an::sampleIsValidByValue<float>(described, std::numeric_limits<float>::infinity()));
    EXPECT_FALSE(an::sampleIsValidByValue<float>(described, -std::numeric_limits<float>::infinity()));
    EXPECT_TRUE(an::sampleIsValidByValue<float>(described, 0.0f));
    EXPECT_TRUE(an::sampleIsValidByValue<float>(described, -0.0f));
    EXPECT_TRUE(an::sampleIsValidByValue<float>(described, (std::numeric_limits<float>::max)()));
    EXPECT_TRUE(an::sampleIsValidByValue<double>(described, -(std::numeric_limits<double>::max)()));
}

TEST(ValidityValueTest, FiniteOnlyOverAnIntegerImageIsTheSameSamplesAsAllValid)
{
    // Stated rather than refused: an image keeps its rule across a conversion, so a rule that
    // depended on the representation would change what an image claims when nothing about it did.
    const an::ValidityDescriptor described = ruleOf(an::ValidityRule::FiniteOnly);
    EXPECT_TRUE(an::sampleIsValidByValue<std::uint8_t>(described, 0u));
    EXPECT_TRUE(an::sampleIsValidByValue<std::int32_t>(described, (std::numeric_limits<std::int32_t>::lowest)()));
    EXPECT_TRUE(an::sampleIsValidByValue<std::uint32_t>(described, (std::numeric_limits<std::uint32_t>::max)()));
}

TEST(ValidityValueTest, ZeroIsNoDataRejectsExactlyZeroAndNothingElse)
{
    const an::ValidityDescriptor described = ruleOf(an::ValidityRule::ZeroIsNoData);
    EXPECT_FALSE(an::sampleIsValidByValue<std::uint16_t>(described, 0u));
    EXPECT_TRUE(an::sampleIsValidByValue<std::uint16_t>(described, 1u));
    EXPECT_FALSE(an::sampleIsValidByValue<float>(described, 0.0f));
    // Negative zero compares equal to zero, so the rule treats the two as one value.
    EXPECT_FALSE(an::sampleIsValidByValue<float>(described, -0.0f));
    EXPECT_TRUE(an::sampleIsValidByValue<float>(described, std::numeric_limits<float>::denorm_min()));
    // The zero rule says nothing about a non-finite sample, which is the finite rule's business.
    EXPECT_TRUE(an::sampleIsValidByValue<float>(described, std::numeric_limits<float>::quiet_NaN()));
}

TEST(ValidityValueTest, NoRuleButTheZeroOneTreatsAZeroAsMeaningless)
{
    for (const an::ValidityRule rule : kAllRules)
    {
        if (rule == an::ValidityRule::ZeroIsNoData)
        {
            continue;
        }
        const an::ValidityDescriptor described = an::validityRuleNeedsRange(rule) ? rangeOf(-1.0, 1.0) : ruleOf(rule);
        EXPECT_TRUE(an::sampleIsValidByValue<std::uint16_t>(described, 0u));
        EXPECT_TRUE(an::sampleIsValidByValue<float>(described, 0.0f));
    }
}

TEST(ValidityValueTest, ADomainRangeIsClosedAtBothEnds)
{
    const an::ValidityDescriptor described = rangeOf(-2.0, 5.0);
    EXPECT_TRUE(an::sampleIsValidByValue<double>(described, -2.0));
    EXPECT_TRUE(an::sampleIsValidByValue<double>(described, 5.0));
    EXPECT_FALSE(an::sampleIsValidByValue<double>(described, std::nextafter(-2.0, -3.0)));
    EXPECT_FALSE(an::sampleIsValidByValue<double>(described, std::nextafter(5.0, 6.0)));
    EXPECT_FALSE(an::sampleIsValidByValue<double>(described, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_TRUE(an::sampleIsValidByValue<std::int16_t>(described, 5));
    EXPECT_FALSE(an::sampleIsValidByValue<std::int16_t>(described, 6));
}

TEST(ValidityValueTest, AConstSampleAnswersTheSameAsAMutableOne)
{
    const an::ValidityDescriptor described = ruleOf(an::ValidityRule::ZeroIsNoData);
    const float sample = 0.0f;
    EXPECT_EQ(an::sampleIsValidByValue<const float>(described, sample),
              an::sampleIsValidByValue<float>(described, sample));
}

TEST(ValidityPolicyTest, EveryPolicyIsOneThisBuildKnowsAndHasItsOwnName)
{
    for (const an::ValidityPolicy left : kAllPolicies)
    {
        EXPECT_TRUE(an::isValidityPolicy(left));
        for (const an::ValidityPolicy right : kAllPolicies)
        {
            const bool sameName = std::strcmp(an::validityPolicyName(left), an::validityPolicyName(right)) == 0;
            EXPECT_EQ(left == right, sameName);
        }
    }
    const an::ValidityPolicy beyond = static_cast<an::ValidityPolicy>(sizeof(kAllPolicies) / sizeof(kAllPolicies[0]));
    EXPECT_FALSE(an::isValidityPolicy(beyond));
    EXPECT_STREQ(an::validityPolicyName(beyond), "UnknownValidityPolicy");
}

TEST(ValidityPolicyTest, APolicyOutsideTheEnumerationIsRefused)
{
    const an::ValidityDescriptor input = ruleOf(an::ValidityRule::AllValid);
    const std::array<an::ValidityDescriptor, 1u> inputs{input};
    const an::ValidityTransition transition{.policy = static_cast<an::ValidityPolicy>(9), .produced = input};
    const an::Result<an::ValidityDescriptor> answered =
        an::applyValidityPolicy(transition, std::span<const an::ValidityDescriptor>{inputs});
    ASSERT_FALSE(answered.hasValue());
    EXPECT_EQ(answered.code(), an::ErrorCode::InvalidArgument);
}

TEST(ValidityPolicyTest, PreserveCarriesOneClaimAcrossAndRefusesTwo)
{
    const an::ValidityDescriptor finite = ruleOf(an::ValidityRule::FiniteOnly);
    const std::array<an::ValidityDescriptor, 2u> agreeing{finite, finite};
    const an::Result<an::ValidityDescriptor> preserved =
        an::applyValidityPolicy(an::ValidityTransition{.policy = an::ValidityPolicy::Preserve},
                                std::span<const an::ValidityDescriptor>{agreeing});
    ASSERT_TRUE(preserved.hasValue());
    EXPECT_EQ(*preserved, finite);

    const std::array<an::ValidityDescriptor, 2u> disagreeing{finite, ruleOf(an::ValidityRule::ZeroIsNoData)};
    const an::Result<an::ValidityDescriptor> refused =
        an::applyValidityPolicy(an::ValidityTransition{.policy = an::ValidityPolicy::Preserve},
                                std::span<const an::ValidityDescriptor>{disagreeing});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ValidityPolicyTest, CombineKeepsAnAgreedClaimAndMaterialisesADisagreementAsAMask)
{
    const an::ValidityDescriptor zeroes = ruleOf(an::ValidityRule::ZeroIsNoData);
    const std::array<an::ValidityDescriptor, 2u> agreeing{zeroes, zeroes};
    const an::Result<an::ValidityDescriptor> agreed =
        an::applyValidityPolicy(an::ValidityTransition{.policy = an::ValidityPolicy::Combine},
                                std::span<const an::ValidityDescriptor>{agreeing});
    ASSERT_TRUE(agreed.hasValue());
    EXPECT_EQ(*agreed, zeroes);

    const std::array<an::ValidityDescriptor, 3u> mixed{an::ValidityDescriptor::allValid(), zeroes,
                                                       ruleOf(an::ValidityRule::FiniteOnly)};
    const an::Result<an::ValidityDescriptor> combined = an::applyValidityPolicy(
        an::ValidityTransition{.policy = an::ValidityPolicy::Combine}, std::span<const an::ValidityDescriptor>{mixed});
    ASSERT_TRUE(combined.hasValue());
    EXPECT_EQ(combined->rule(), an::ValidityRule::ExplicitMask);
    EXPECT_TRUE(combined->needsMask());
}

TEST(ValidityPolicyTest, TwoRangesThatDifferCombineToAMaskRatherThanToAGuessedIntersection)
{
    const std::array<an::ValidityDescriptor, 2u> ranges{rangeOf(0.0, 1.0), rangeOf(0.5, 2.0)};
    const an::Result<an::ValidityDescriptor> combined = an::applyValidityPolicy(
        an::ValidityTransition{.policy = an::ValidityPolicy::Combine}, std::span<const an::ValidityDescriptor>{ranges});
    ASSERT_TRUE(combined.hasValue());
    EXPECT_EQ(combined->rule(), an::ValidityRule::ExplicitMask);
}

TEST(ValidityPolicyTest, RegenerateAnswersWithWhatTheOperationStatesAndReadsNoInput)
{
    const an::ValidityDescriptor produced = rangeOf(-1.0, 1.0);
    const an::Result<an::ValidityDescriptor> regenerated =
        an::applyValidityPolicy(an::ValidityTransition{.policy = an::ValidityPolicy::Regenerate, .produced = produced},
                                std::span<const an::ValidityDescriptor>{});
    ASSERT_TRUE(regenerated.hasValue());
    EXPECT_EQ(*regenerated, produced);
}

TEST(ValidityPolicyTest, RejectRefusesAnyInputThatIsNotAllValid)
{
    const std::array<an::ValidityDescriptor, 2u> complete{an::ValidityDescriptor::allValid(),
                                                          an::ValidityDescriptor::allValid()};
    const an::Result<an::ValidityDescriptor> accepted =
        an::applyValidityPolicy(an::ValidityTransition{.policy = an::ValidityPolicy::Reject},
                                std::span<const an::ValidityDescriptor>{complete});
    ASSERT_TRUE(accepted.hasValue());
    EXPECT_TRUE(accepted->isAllValid());

    const std::array<an::ValidityDescriptor, 2u> partial{an::ValidityDescriptor::allValid(),
                                                         ruleOf(an::ValidityRule::ExplicitMask)};
    const an::Result<an::ValidityDescriptor> refused = an::applyValidityPolicy(
        an::ValidityTransition{.policy = an::ValidityPolicy::Reject}, std::span<const an::ValidityDescriptor>{partial});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    const an::StatusContextField* const which = refused.status().findContext(an::ContextKey::Completed);
    ASSERT_NE(which, nullptr);
    EXPECT_EQ(which->count, 1u);
}

TEST(ValidityPolicyTest, APolicyThatDerivesFromItsInputsRefusesWhenGivenNone)
{
    for (const an::ValidityPolicy policy :
         {an::ValidityPolicy::Preserve, an::ValidityPolicy::Combine, an::ValidityPolicy::Reject})
    {
        const an::Result<an::ValidityDescriptor> answered = an::applyValidityPolicy(
            an::ValidityTransition{.policy = policy}, std::span<const an::ValidityDescriptor>{});
        ASSERT_FALSE(answered.hasValue());
        EXPECT_EQ(answered.code(), an::ErrorCode::InvalidArgument);
    }
}

TEST(ValiditySelectionTest, ACountKnowsWhetherItIsEmptyAndWhetherItIsComplete)
{
    constexpr an::ValidityCount nothing{};
    static_assert(nothing.empty());
    static_assert(nothing.complete());

    constexpr an::ValidityCount partial{.valid = 3u, .total = 9u};
    static_assert(!partial.empty());
    static_assert(!partial.complete());

    constexpr an::ValidityCount whole{.valid = 9u, .total = 9u};
    static_assert(!whole.empty());
    static_assert(whole.complete());

    constexpr an::ValidityCount none{.valid = 0u, .total = 9u};
    static_assert(none.empty());
    static_assert(!none.complete());
}

TEST(ValiditySelectionTest, AReductionOverNothingIsAnOutcomeRatherThanANumber)
{
    const an::Status refused = an::requireSelection(an::ValidityCount{.valid = 0u, .total = 4096u}, "test::reduce");
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(refused.category(), an::ErrorCategory::Data);
    EXPECT_FALSE(refused.isCancellation());
    EXPECT_STREQ(refused.operation(), "test::reduce");

    const an::StatusContextField* const selected = refused.findContext(an::ContextKey::Completed);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->count, 0u);
    const an::StatusContextField* const considered = refused.findContext(an::ContextKey::Total);
    ASSERT_NE(considered, nullptr);
    EXPECT_EQ(considered->count, 4096u);
}

TEST(ValiditySelectionTest, OneSurvivingSampleIsEnoughToReduce)
{
    EXPECT_TRUE(an::requireSelection(an::ValidityCount{.valid = 1u, .total = 9000000u}, "test::reduce").ok());
}
