// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the declarations the core operation surface publishes beside its entry points.
//
// Two things are proved here that neither the vocabulary's own tests nor an operation's behaviour
// tests can. First, every published declaration is one an operation could actually have: a constant
// nobody reads is a constant nobody notices is wrong, so each is put through the same check
// `operationTransition` runs. Second, the two computed declarations answer differently for
// different plans, which is the whole reason they are functions.

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramBuilder.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/Histogram.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Image/ChannelTransfer.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace
{

/// The plan shape the pointwise declarations are exercised with.
///
/// @param channels how many channels the chain reads and writes.
/// @return a three-channel Float32 shape in the normalized linear domain.
[[nodiscard]] an::PointwisePlanShape planShape(std::uint32_t channels = 3u) noexcept
{
    an::PointwisePlanShape shape;
    shape.channelCount = channels;
    return shape;
}

/// A three-channel colour encoding, stated the way a caller states one.
///
/// @param transfer how the numbers relate to light.
/// @return an sRGB-primaries encoding under that transfer function, defaulted where the schema does
///         not read a field.
[[nodiscard]] an::ColorEncoding encodingOf(an::ColorTransferFunction transfer) noexcept
{
    an::ColorEncoding encoding;
    encoding.schema = an::ChannelSchema::Rgb;
    encoding.primaries = an::RgbPrimaries::Srgb;
    encoding.transfer = transfer;
    return encoding;
}

/// The tone-plan shape a curve is lowered against, with both domains stated the same.
///
/// A curve whose input and output are both normalized linear changes what a sample means without
/// changing which domain it is in, which is exactly the case that separates a restated meaning from
/// a restated description.
///
/// @return a one-channel Float32 shape whose two domains are equal.
[[nodiscard]] an::TonePlanShape sameDomainToneShape() noexcept
{
    an::TonePlanShape shape;
    shape.sampleType = an::SampleType::Float32;
    shape.inputDomain = an::NumericDomain::NormalizedLinear;
    shape.outputDomain = an::NumericDomain::NormalizedLinear;
    shape.channelCount = 1u;
    return shape;
}

} // namespace

TEST(OperationFacadeTest, EveryPublishedDeclarationIsOneAnOperationCouldHave)
{
    static_assert(an::checkOperationEffect(an::kImageTransferEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kChannelExtractEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kChannelComposeEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kBasicStatisticsEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kRobustStatisticsEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kImageHistogramEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kMaskInspectionEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kApplyImageMaskEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kBinarizeImageEffect) == an::OperationEffectError::None);
    static_assert(an::checkOperationEffect(an::kPixelMathExecutionEffect) == an::OperationEffectError::None);

    // Restated deliberately as a run-time expectation as well: a static assertion that stops
    // compiling is a build failure with no test name attached to it.
    EXPECT_EQ(an::checkOperationEffect(an::kImageTransferEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::checkOperationEffect(an::kPixelMathExecutionEffect), an::OperationEffectError::None);
}

TEST(OperationFacadeTest, AReductionDeclaresThatItChangesNothing)
{
    for (const an::OperationEffect& effect :
         std::array<an::OperationEffect, 4u>{an::kBasicStatisticsEffect, an::kRobustStatisticsEffect,
                                             an::kImageHistogramEffect, an::kMaskInspectionEffect})
    {
        EXPECT_EQ(effect.samples, an::SampleEffect::Reads);
        EXPECT_EQ(effect.description, an::DescriptionEffect::Preserves);
        EXPECT_EQ(effect.semantics, an::SemanticEffect::Preserves);
        EXPECT_EQ(effect.validity.policy, an::ValidityPolicy::Preserve);
    }
}

TEST(OperationFacadeTest, MovingSamplesChangesNothingAndTakingOneChannelChangesTheDescription)
{
    EXPECT_EQ(an::kImageTransferEffect, an::OperationEffect{});

    EXPECT_EQ(an::kChannelExtractEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(an::kChannelExtractEffect.semantics, an::SemanticEffect::Preserves);
    EXPECT_EQ(an::kChannelExtractEffect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(an::kChannelExtractEffect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(an::kChannelExtractEffect.validity.policy, an::ValidityPolicy::Preserve);

    EXPECT_EQ(an::kChannelComposeEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(an::kChannelComposeEffect.validity.policy, an::ValidityPolicy::Combine);
}

TEST(OperationFacadeTest, AScaleDeclaresLessThanAGammaAndAGammaLessThanAMatrix)
{
    const an::Result<an::PointwiseProgram> scale =
        an::buildScaleBiasProgram(planShape(), 1.5, 0.0, an::ClampInterval{.low = 0.0, .high = 1.0});
    ASSERT_TRUE(scale.hasValue()) << scale.status().message();
    const an::OperationEffect scaleEffect = an::pointwiseProgramEffect(*scale);
    EXPECT_EQ(an::checkOperationEffect(scaleEffect), an::OperationEffectError::None);
    EXPECT_EQ(scaleEffect, an::OperationEffect{});

    // A curve whose two domains are the same changes what the numbers mean and not which domain
    // they are in, so the meaning is restated and the description is not.
    const std::array<an::CurveControlPoint, 3u> points{an::CurveControlPoint{.input = 0.0, .output = 0.0},
                                                       an::CurveControlPoint{.input = 0.5, .output = 0.25},
                                                       an::CurveControlPoint{.input = 1.0, .output = 1.0}};
    const an::Result<an::CurveProgramPlan> curve = an::buildCurveProgram(
        sameDomainToneShape(), an::CurveProgramRequest{.points = std::span<const an::CurveControlPoint>{points}});
    ASSERT_TRUE(curve.hasValue()) << curve.status().message();
    const an::OperationEffect curveEffect = an::pointwiseProgramEffect(curve->program());
    EXPECT_EQ(an::checkOperationEffect(curveEffect), an::OperationEffectError::None);
    EXPECT_EQ(curveEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(curveEffect.description, an::DescriptionEffect::Preserves);
    EXPECT_EQ(curveEffect.codecMetadata, an::CodecMetadataEffect::Preserves);

    // A gamma encode moves the samples out of the linear domain as well, so it restates both.
    const an::Result<an::PowerProgramPlan> gamma =
        an::buildGammaProgram(an::TonePlanShape{.channelCount = 1u}, 2.2, an::GammaDirection::Encode);
    ASSERT_TRUE(gamma.hasValue()) << gamma.status().message();
    const an::OperationEffect gammaEffect = an::pointwiseProgramEffect(gamma->program());
    EXPECT_EQ(an::checkOperationEffect(gammaEffect), an::OperationEffectError::None);
    EXPECT_EQ(gammaEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(gammaEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(gammaEffect.codecMetadata, an::CodecMetadataEffect::Drops);

    const std::array<double, 9u> identityRows{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    const an::Result<an::PointwiseProgram> matrix =
        an::buildChannelMatrixProgram(planShape(), std::span<const double>{identityRows});
    ASSERT_TRUE(matrix.hasValue()) << matrix.status().message();
    const an::OperationEffect matrixEffect = an::pointwiseProgramEffect(*matrix);
    EXPECT_EQ(an::checkOperationEffect(matrixEffect), an::OperationEffectError::None);
    EXPECT_EQ(matrixEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(matrixEffect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(matrixEffect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(matrixEffect.solution, an::SolutionEffect::Preserves);
}

TEST(OperationFacadeTest, ASecondSourceMakesAChainCombineItsInputsValidity)
{
    const an::Result<an::PointwiseProgram> add =
        an::buildBinaryArithmeticProgram(planShape(), an::BinaryArithmeticOperation::Add);
    ASSERT_TRUE(add.hasValue()) << add.status().message();
    ASSERT_EQ(add->sourceCount(), 2u);

    const an::OperationEffect effect = an::pointwiseProgramEffect(*add);
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::None);
    EXPECT_EQ(effect.validity.policy, an::ValidityPolicy::Combine);
}

TEST(OperationFacadeTest, AnIdentityColourPlanDeclaresACopyAndARealOneDeclaresANewImage)
{
    const an::ColorEncoding linear = encodingOf(an::ColorTransferFunction::Linear);
    const an::Result<an::ColorConversionPlan> identity = an::planColorConversion(linear, linear);
    ASSERT_TRUE(identity.hasValue()) << identity.status().message();
    ASSERT_TRUE(identity->isIdentity());
    EXPECT_EQ(an::colorConversionEffect(*identity), an::OperationEffect{});

    const an::ColorEncoding encoded = encodingOf(an::ColorTransferFunction::Srgb);
    const an::Result<an::ColorConversionPlan> real = an::planColorConversion(encoded, linear);
    ASSERT_TRUE(real.hasValue()) << real.status().message();
    ASSERT_FALSE(real->isIdentity());

    const an::OperationEffect effect = an::colorConversionEffect(*real);
    EXPECT_EQ(an::checkOperationEffect(effect), an::OperationEffectError::None);
    EXPECT_EQ(effect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(effect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(effect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(effect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(effect.solution, an::SolutionEffect::Preserves);
}

TEST(OperationFacadeTest, ADeclarationAndAnOutcomeMakeTheTransitionAnImageIsGiven)
{
    const an::Result<an::PowerProgramPlan> gamma =
        an::buildGammaProgram(an::TonePlanShape{.channelCount = 1u}, 2.2, an::GammaDirection::Encode);
    ASSERT_TRUE(gamma.hasValue()) << gamma.status().message();
    const an::OperationEffect effect = an::pointwiseProgramEffect(gamma->program());

    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::PureGamma;
    request.gammaExponent = 2.2;
    const an::Result<an::ImageSemantics> stretched = an::ImageSemantics::create(request);
    ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();

    const an::Result<an::MetadataTransition> change = an::operationTransition(
        effect, an::OperationOutcome{
                    .operation = "an::executePointwiseProgram", .detail = "gamma 2.2", .semantics = *stretched});
    ASSERT_TRUE(change.hasValue()) << change.status().message();
    EXPECT_TRUE(an::changesMeaning(*change));
    ASSERT_TRUE(change->semantics.has_value());
    EXPECT_EQ(change->semantics->transferFunction(), an::TransferFunction::PureGamma);

    // And the half a call site cannot get away with: the same chain, reported without the meaning it
    // just created.
    EXPECT_EQ(an::checkOperationOutcome(effect, an::OperationOutcome{.operation = "an::executePointwiseProgram"}),
              an::OperationOutcomeError::MissingSemantics);
}
