// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for what an operation declares it does to an image's meaning and validity, and
// for the transition that declaration turns into.
//
// The two halves are tested together because they are one contract read from two sides: a
// declaration that cannot describe an operation must be refused wherever it is read, and an outcome
// that disagrees with a consistent declaration must be refused only where the two meet.

#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace
{

/// The meaning a calibrated frame carries, built the way an operation that restates one would.
///
/// @return linear semantics with the dark-subtracted flag set.
[[nodiscard]] an::ImageSemantics calibratedSemantics()
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.darkSubtracted = true;
    const an::Result<an::ImageSemantics> made = an::ImageSemantics::create(request);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? *made : an::ImageSemantics{};
}

/// The validity rule an image with no invalid samples claims.
///
/// @return the all-valid description.
[[nodiscard]] an::ValidityDescriptor allValid() noexcept
{
    return an::ValidityDescriptor::allValid();
}

/// The validity rule an image whose non-finite samples mean nothing claims.
///
/// @return the finite-only description.
[[nodiscard]] an::ValidityDescriptor finiteOnly()
{
    const std::optional<an::ValidityDescriptor> made =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    EXPECT_TRUE(made.has_value());
    return made.value_or(an::ValidityDescriptor{});
}

} // namespace

TEST(OperationEffectTest, DefaultDeclarationWritesSamplesAndChangesNothingElse)
{
    constexpr an::OperationEffect effect{};

    static_assert(effect.samples == an::SampleEffect::Writes);
    static_assert(effect.description == an::DescriptionEffect::Preserves);
    static_assert(effect.semantics == an::SemanticEffect::Preserves);
    static_assert(effect.solution == an::SolutionEffect::Preserves);
    static_assert(effect.codecMetadata == an::CodecMetadataEffect::Preserves);
    static_assert(effect.outputIntent == an::OutputIntentEffect::Preserves);
    static_assert(effect.validity.policy == an::ValidityPolicy::Preserve);
    static_assert(an::checkOperationEffect(effect) == an::OperationEffectError::None);
}

TEST(OperationEffectTest, EveryEffectNamesItselfAndRefusesAValueOutsideItsEnumeration)
{
    EXPECT_STREQ(an::sampleEffectName(an::SampleEffect::Reads), "Reads");
    EXPECT_STREQ(an::sampleEffectName(static_cast<an::SampleEffect>(9u)), "UnknownSampleEffect");
    EXPECT_STREQ(an::descriptionEffectName(an::DescriptionEffect::Restates), "Restates");
    EXPECT_STREQ(an::descriptionEffectName(static_cast<an::DescriptionEffect>(9u)), "UnknownDescriptionEffect");
    EXPECT_STREQ(an::semanticEffectName(an::SemanticEffect::Restates), "Restates");
    EXPECT_STREQ(an::semanticEffectName(static_cast<an::SemanticEffect>(9u)), "UnknownSemanticEffect");
    EXPECT_STREQ(an::solutionEffectName(an::SolutionEffect::Invalidates), "Invalidates");
    EXPECT_STREQ(an::solutionEffectName(static_cast<an::SolutionEffect>(9u)), "UnknownSolutionEffect");
    EXPECT_STREQ(an::codecMetadataEffectName(an::CodecMetadataEffect::Drops), "Drops");
    EXPECT_STREQ(an::codecMetadataEffectName(static_cast<an::CodecMetadataEffect>(9u)), "UnknownCodecMetadataEffect");
    EXPECT_STREQ(an::outputIntentEffectName(an::OutputIntentEffect::Withdraws), "Withdraws");
    EXPECT_STREQ(an::outputIntentEffectName(static_cast<an::OutputIntentEffect>(9u)), "UnknownOutputIntentEffect");
    EXPECT_STREQ(an::operationEffectErrorName(an::OperationEffectError::UnknownEffect), "UnknownEffect");
    EXPECT_STREQ(an::operationEffectErrorName(static_cast<an::OperationEffectError>(99u)),
                 "UnknownOperationEffectError");
    EXPECT_STREQ(an::operationOutcomeErrorName(an::OperationOutcomeError::MissingSemantics), "MissingSemantics");
    EXPECT_STREQ(an::operationOutcomeErrorName(static_cast<an::OperationOutcomeError>(99u)),
                 "UnknownOperationOutcomeError");

    EXPECT_FALSE(an::isSampleEffect(static_cast<an::SampleEffect>(9u)));
    EXPECT_FALSE(an::isDescriptionEffect(static_cast<an::DescriptionEffect>(9u)));
    EXPECT_FALSE(an::isSemanticEffect(static_cast<an::SemanticEffect>(9u)));
    EXPECT_FALSE(an::isSolutionEffect(static_cast<an::SolutionEffect>(9u)));
    EXPECT_FALSE(an::isCodecMetadataEffect(static_cast<an::CodecMetadataEffect>(9u)));
    EXPECT_FALSE(an::isOutputIntentEffect(static_cast<an::OutputIntentEffect>(9u)));
    EXPECT_FALSE(an::isOperationOutcomeError(static_cast<an::OperationOutcomeError>(99u)));
}

TEST(OperationEffectTest, AReductionThatClaimsToChangeItsResultIsRefused)
{
    an::OperationEffect effect{};
    effect.samples = an::SampleEffect::Reads;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::None);

    for (const an::OperationEffect changed :
         std::array<an::OperationEffect, 5u>{[effect] {
                                                 an::OperationEffect one = effect;
                                                 one.description = an::DescriptionEffect::Restates;
                                                 return one;
                                             }(),
                                             [effect] {
                                                 an::OperationEffect one = effect;
                                                 one.semantics = an::SemanticEffect::Restates;
                                                 return one;
                                             }(),
                                             [effect] {
                                                 an::OperationEffect one = effect;
                                                 one.solution = an::SolutionEffect::Invalidates;
                                                 return one;
                                             }(),
                                             [effect] {
                                                 an::OperationEffect one = effect;
                                                 one.codecMetadata = an::CodecMetadataEffect::Drops;
                                                 return one;
                                             }(),
                                             [effect] {
                                                 an::OperationEffect one = effect;
                                                 one.outputIntent = an::OutputIntentEffect::Withdraws;
                                                 return one;
                                             }()})
    {
        EXPECT_EQ(an::checkOperationEffect(changed), an::OperationEffectError::ReadingOperationChangesResult);
    }
}

TEST(OperationEffectTest, AReductionMayOnlyPreserveOrRejectValidity)
{
    an::OperationEffect effect{};
    effect.samples = an::SampleEffect::Reads;

    effect.validity.policy = an::ValidityPolicy::Reject;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::None);

    effect.validity.policy = an::ValidityPolicy::Combine;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::ReadingOperationRegeneratesValidity);

    effect.validity.policy = an::ValidityPolicy::Regenerate;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::ReadingOperationRegeneratesValidity);
}

TEST(OperationEffectTest, AStatedValidityRuleNeedsThePolicyThatReadsIt)
{
    an::OperationEffect effect{};
    effect.validity.produced = finiteOnly();

    effect.validity.policy = an::ValidityPolicy::Preserve;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::ProducedValidityWithoutRegenerate);

    effect.validity.policy = an::ValidityPolicy::Combine;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::ProducedValidityWithoutRegenerate);

    effect.validity.policy = an::ValidityPolicy::Regenerate;
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::None);
}

TEST(OperationEffectTest, AValueOutsideAnEnumerationIsRefusedBeforeAnythingElse)
{
    an::OperationEffect effect{};
    effect.description = static_cast<an::DescriptionEffect>(9u);
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::UnknownEffect);

    an::OperationEffect policy{};
    policy.validity.policy = static_cast<an::ValidityPolicy>(9u);
    EXPECT_EQ(an::checkOperationEffect(policy), an::OperationEffectError::UnknownValidityPolicy);
}

TEST(OperationEffectTest, DeclaredValidityIsAppliedRatherThanChosenAtTheCallSite)
{
    an::OperationEffect preserve{};
    const std::array<an::ValidityDescriptor, 2u> agreeing{allValid(), allValid()};
    const an::Result<an::ValidityDescriptor> preserved =
        an::operationValidity(preserve, std::span<const an::ValidityDescriptor>{agreeing});
    ASSERT_TRUE(preserved.hasValue()) << preserved.status().message();
    EXPECT_EQ(preserved->rule(), an::ValidityRule::AllValid);

    const std::array<an::ValidityDescriptor, 2u> disagreeing{allValid(), finiteOnly()};
    EXPECT_EQ(an::operationValidity(preserve, std::span<const an::ValidityDescriptor>{disagreeing}).status().code(),
              an::ErrorCode::IncompatibleArguments);

    an::OperationEffect combine{};
    combine.validity.policy = an::ValidityPolicy::Combine;
    const an::Result<an::ValidityDescriptor> combined =
        an::operationValidity(combine, std::span<const an::ValidityDescriptor>{disagreeing});
    ASSERT_TRUE(combined.hasValue()) << combined.status().message();
    EXPECT_EQ(combined->rule(), an::ValidityRule::ExplicitMask);
}

TEST(OperationEffectTest, AnInconsistentDeclarationIsRefusedBeforeItsInputsAreRead)
{
    an::OperationEffect effect{};
    effect.samples = an::SampleEffect::Reads;
    effect.validity.policy = an::ValidityPolicy::Combine;

    const std::array<an::ValidityDescriptor, 1u> inputs{allValid()};
    const an::Result<an::ValidityDescriptor> refused =
        an::operationValidity(effect, std::span<const an::ValidityDescriptor>{inputs});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(refused.status().message()).find("ReadingOperationRegeneratesValidity"), std::string::npos);
}

TEST(OperationTransitionTest, EveryTransitionCarriesTheLineageEntryAnImageRefusesToMoveWithout)
{
    constexpr an::OperationEffect effect{};
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        effect, an::OperationOutcome{.operation = "an::fillImageSamples", .detail = "0.5", .semantics = std::nullopt});
    ASSERT_TRUE(change.hasValue()) << change.status().message();
    ASSERT_TRUE(change->record.has_value());
    EXPECT_EQ(change->record->operation(), "an::fillImageSamples");
    EXPECT_EQ(change->record->detail(), "0.5");
    EXPECT_FALSE(change->semantics.has_value());
    EXPECT_FALSE(change->astrometry.has_value());
    EXPECT_FALSE(change->clearCodecMetadata);
    EXPECT_FALSE(change->clearOutputIntent);
    // A transition that only records what happened states no change at all, which is the correct
    // answer for an operation that wrote samples and left every metadata object alone: the lineage
    // grows, and nothing that reads metadata has to be told anything.
    EXPECT_FALSE(an::changesAnything(*change));
    EXPECT_FALSE(an::changesMeaning(*change));
}

TEST(OperationTransitionTest, ARestatedMeaningTravelsAndMovesTheSemanticGeneration)
{
    an::OperationEffect effect{};
    effect.semantics = an::SemanticEffect::Restates;

    const an::Result<an::MetadataTransition> change =
        an::operationTransition(effect, an::OperationOutcome{.operation = "an::executePointwiseProgram",
                                                             .detail = "subtract master dark",
                                                             .semantics = calibratedSemantics()});
    ASSERT_TRUE(change.hasValue()) << change.status().message();
    ASSERT_TRUE(change->semantics.has_value());
    EXPECT_TRUE(change->semantics->processingState().darkSubtracted);
    EXPECT_TRUE(an::changesMeaning(*change));
}

TEST(OperationTransitionTest, AnInvalidatedSolutionIsStatedAsAnEmptyOneRatherThanLeftAlone)
{
    an::OperationEffect effect{};
    effect.solution = an::SolutionEffect::Invalidates;
    effect.codecMetadata = an::CodecMetadataEffect::Drops;
    effect.outputIntent = an::OutputIntentEffect::Withdraws;

    const an::Result<an::MetadataTransition> change = an::operationTransition(
        effect, an::OperationOutcome{.operation = "an::convertImageColor", .detail = "", .semantics = std::nullopt});
    ASSERT_TRUE(change.hasValue()) << change.status().message();
    ASSERT_TRUE(change->astrometry.has_value());
    EXPECT_FALSE(change->astrometry->present());
    EXPECT_TRUE(change->clearCodecMetadata);
    EXPECT_TRUE(change->clearOutputIntent);
    EXPECT_TRUE(an::changesMeaning(*change));
}

TEST(OperationTransitionTest, AnOutcomeThatDisagreesWithItsDeclarationIsRefusedBothWays)
{
    an::OperationEffect restating{};
    restating.semantics = an::SemanticEffect::Restates;
    EXPECT_EQ(an::checkOperationOutcome(restating, an::OperationOutcome{.operation = "an::op"}),
              an::OperationOutcomeError::MissingSemantics);
    const an::Result<an::MetadataTransition> missing =
        an::operationTransition(restating, an::OperationOutcome{.operation = "an::op"});
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_NE(std::string(missing.status().message()).find("MissingSemantics"), std::string::npos);

    constexpr an::OperationEffect preserving{};
    const an::OperationOutcome stated{.operation = "an::op", .detail = "", .semantics = calibratedSemantics()};
    EXPECT_EQ(an::checkOperationOutcome(preserving, stated), an::OperationOutcomeError::UnexpectedSemantics);
    const an::Result<an::MetadataTransition> unexpected = an::operationTransition(preserving, stated);
    ASSERT_FALSE(unexpected.hasValue());
    EXPECT_NE(std::string(unexpected.status().message()).find("UnexpectedSemantics"), std::string::npos);
}

TEST(OperationTransitionTest, AnInconsistentDeclarationAndAnEmptyLineageAreBothRefused)
{
    an::OperationEffect inconsistent{};
    inconsistent.samples = an::SampleEffect::Reads;
    inconsistent.description = an::DescriptionEffect::Restates;
    const an::Result<an::MetadataTransition> declaration =
        an::operationTransition(inconsistent, an::OperationOutcome{.operation = "an::op"});
    ASSERT_FALSE(declaration.hasValue());
    EXPECT_NE(std::string(declaration.status().message()).find("ReadingOperationChangesResult"), std::string::npos);

    constexpr an::OperationEffect effect{};
    const an::Result<an::MetadataTransition> nameless =
        an::operationTransition(effect, an::OperationOutcome{.operation = ""});
    ASSERT_FALSE(nameless.hasValue());
    EXPECT_EQ(nameless.status().code(), an::ErrorCode::InvalidArgument);
}
