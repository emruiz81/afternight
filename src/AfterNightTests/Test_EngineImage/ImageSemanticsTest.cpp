// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageSemantics.h.
//
// Two rules carry this type and both are checked in both directions.
//
// **A power law always has its exponent, and nothing else ever does.** The first half is obvious;
// the second is the one that gets left out, and it is the one that leaves a number sitting beside
// sRGB for something later to read as a gamma.
//
// **Linear is a claim, and unspecified is not evidence of it.** Every physical computation asks
// whether an image is linear, and an image that never said so has to answer no - which is what
// `isLinear` returning false for `Unspecified` is about.
//
// The validity description is the third thing this object carries, and it is here rather than in the
// image descriptor because it is the same kind of fact as the other two: what has happened to these
// samples. The cases below assert that a freshly decoded frame claims every sample means something,
// that a stated rule survives the round trip through the request, and that two meanings carrying
// different rules are different meanings - which is what makes a change of validity move an image's
// semantic generation and invalidate what was computed under the old rule.

#include <AfterNightEngine/Image/ImageSemantics.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <optional>

namespace
{

constexpr an::TransferFunction kAllTransfers[] = {an::TransferFunction::Unspecified, an::TransferFunction::Linear,
                                                  an::TransferFunction::Srgb, an::TransferFunction::Rec709,
                                                  an::TransferFunction::PureGamma};

TEST(ImageSemanticsTest, TheDefaultDescribesAFreshlyDecodedFrame)
{
    const an::ImageSemantics semantics;
    EXPECT_EQ(semantics.transferFunction(), an::TransferFunction::Unspecified);
    EXPECT_FALSE(semantics.gammaExponent().has_value());
    EXPECT_FALSE(semantics.isLinear());

    const an::ProcessingState& state = semantics.processingState();
    EXPECT_FALSE(state.biasSubtracted);
    EXPECT_FALSE(state.darkSubtracted);
    EXPECT_FALSE(state.flatFielded);
    EXPECT_FALSE(state.cosmeticCorrected);
    EXPECT_FALSE(state.demosaiced);
    EXPECT_FALSE(state.backgroundExtracted);
    EXPECT_FALSE(state.registered);
    EXPECT_FALSE(state.normalized);
    EXPECT_FALSE(state.colorCalibrated);
}

TEST(ImageSemanticsTest, WhatHasBeenAppliedIsCarriedAndReadsBackUnchanged)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.biasSubtracted = true;
    request.processingState.darkSubtracted = true;
    request.processingState.flatFielded = true;
    request.processingState.demosaiced = true;

    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();

    EXPECT_TRUE(semantics->isLinear());
    EXPECT_TRUE(semantics->processingState().flatFielded);
    EXPECT_TRUE(semantics->processingState().demosaiced);
    EXPECT_FALSE(semantics->processingState().registered);
    EXPECT_FALSE(semantics->gammaExponent().has_value());
}

TEST(ImageSemanticsTest, APowerLawCarriesItsExponent)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::PureGamma;
    request.gammaExponent = 2.2;

    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    ASSERT_TRUE(semantics->gammaExponent().has_value());
    EXPECT_DOUBLE_EQ(*semantics->gammaExponent(), 2.2);
    EXPECT_FALSE(semantics->isLinear());
}

TEST(ImageSemanticsTest, APowerLawWithoutAnExponentCannotBeAppliedOrRemoved)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::PureGamma;

    an::Result<an::ImageSemantics> refused = an::ImageSemantics::create(request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    const an::StatusContextField* const field = refused.status().findContext(an::ContextKey::Parameter);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->text, "gammaExponent");
}

TEST(ImageSemanticsTest, AnExponentThatIsNotOneIsRefused)
{
    for (const double exponent : {0.0, -2.2, std::numeric_limits<double>::infinity()})
    {
        an::ImageSemanticsRequest request;
        request.transferFunction = an::TransferFunction::PureGamma;
        request.gammaExponent = exponent;

        an::Result<an::ImageSemantics> refused = an::ImageSemantics::create(request);
        ASSERT_FALSE(refused.hasValue()) << exponent;
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument) << exponent;
    }
}

TEST(ImageSemanticsTest, AnExponentBesideAnEncodingThatIsNotAPowerLawIsRefused)
{
    for (const an::TransferFunction transfer : kAllTransfers)
    {
        if (transfer == an::TransferFunction::PureGamma)
        {
            continue;
        }
        an::ImageSemanticsRequest request;
        request.transferFunction = transfer;
        request.gammaExponent = 2.2;

        an::Result<an::ImageSemantics> refused = an::ImageSemantics::create(request);
        ASSERT_FALSE(refused.hasValue()) << an::transferFunctionName(transfer);
        EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments) << an::transferFunctionName(transfer);
    }
}

TEST(ImageSemanticsTest, OnlyLinearClaimsToBeLinear)
{
    for (const an::TransferFunction transfer : kAllTransfers)
    {
        an::ImageSemanticsRequest request;
        request.transferFunction = transfer;
        if (transfer == an::TransferFunction::PureGamma)
        {
            request.gammaExponent = 2.2;
        }

        an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
        ASSERT_TRUE(semantics.hasValue()) << an::transferFunctionName(transfer);
        EXPECT_EQ(semantics->isLinear(), transfer == an::TransferFunction::Linear)
            << an::transferFunctionName(transfer);
    }
}

TEST(ImageSemanticsTest, AnEncodingOutsideTheEnumerationIsRefused)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = static_cast<an::TransferFunction>(200);

    an::Result<an::ImageSemantics> refused = an::ImageSemantics::create(request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_FALSE(an::isTransferFunction(static_cast<an::TransferFunction>(200)));
}

TEST(ImageSemanticsTest, TheMeaningRoundTripsThroughItsRequest)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::PureGamma;
    request.gammaExponent = 1.8;
    request.processingState.registered = true;

    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();

    an::Result<an::ImageSemantics> rebuilt = an::ImageSemantics::create(semantics->toRequest());
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.status().message();
    EXPECT_EQ(*rebuilt, *semantics);
}

TEST(ImageSemanticsTest, TwoMeaningsDifferWhenAnyFlagDoes)
{
    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;

    an::Result<an::ImageSemantics> left = an::ImageSemantics::create(request);
    ASSERT_TRUE(left.hasValue());

    request.processingState.normalized = true;
    an::Result<an::ImageSemantics> right = an::ImageSemantics::create(request);
    ASSERT_TRUE(right.hasValue());

    EXPECT_NE(*left, *right);
}

TEST(ImageSemanticsTest, EveryEncodingHasItsOwnNameAndAStrangerSaysSo)
{
    for (const an::TransferFunction left : kAllTransfers)
    {
        EXPECT_TRUE(an::isTransferFunction(left));
        for (const an::TransferFunction right : kAllTransfers)
        {
            const bool sameName = std::strcmp(an::transferFunctionName(left), an::transferFunctionName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_STREQ(an::transferFunctionName(static_cast<an::TransferFunction>(200)), "UnknownTransferFunction");
}

TEST(ImageSemanticsTest, AFreshlyDecodedFrameClaimsEverySampleMeansSomething)
{
    const an::ImageSemantics semantics;
    EXPECT_TRUE(semantics.validity().isAllValid());
    EXPECT_FALSE(semantics.validity().needsMask());
}

TEST(ImageSemanticsTest, AStatedValidityRuleSurvivesTheRoundTripThroughTheRequest)
{
    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(finite.has_value());

    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;
    request.processingState.flatFielded = true;
    request.validity = *finite;

    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(request);
    ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
    EXPECT_EQ(semantics->validity(), *finite);

    an::Result<an::ImageSemantics> rebuilt = an::ImageSemantics::create(semantics->toRequest());
    ASSERT_TRUE(rebuilt.hasValue()) << rebuilt.status().message();
    EXPECT_EQ(*rebuilt, *semantics);
    EXPECT_EQ(rebuilt->validity(), *finite);
}

TEST(ImageSemanticsTest, TwoMeaningsDifferWhenOnlyTheValidityRuleDoes)
{
    const std::optional<an::ValidityDescriptor> zeroes =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(zeroes.has_value());

    an::ImageSemanticsRequest request;
    request.transferFunction = an::TransferFunction::Linear;

    an::Result<an::ImageSemantics> left = an::ImageSemantics::create(request);
    ASSERT_TRUE(left.hasValue());

    request.validity = *zeroes;
    an::Result<an::ImageSemantics> right = an::ImageSemantics::create(request);
    ASSERT_TRUE(right.hasValue());

    EXPECT_NE(*left, *right);
}

} // namespace
