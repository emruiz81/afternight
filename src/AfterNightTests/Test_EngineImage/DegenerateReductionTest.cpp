// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Tests for two assumptions a reduction over an image must not make.
//
// Both are the same habit twice. A scan that seeds a running minimum at 1.0 and a running maximum at
// 0.0 is only correct if every sample is known to lie in the closed unit interval - and dividing by
// their difference is only defined if the image is not constant. An image of all zeros would come
// out with a minimum of one and a maximum of zero, and a normalisation of it would divide by minus
// one, or by zero; a frame of raw sensor codes would come out with extremes that are not in the data
// at all.
//
// The routines that normalise are not written here, and their result types are decided where they
// are. What is decided *here* is the two assumptions underneath them, and both are things this
// engine's image model can already be asked about.
//
// **Where the range comes from.** An image states what its magnitudes mean. Two of the four
// meanings are explicitly not fractions of full scale, so there is no interval a reduction may seed
// itself from without reading the data - and a value outside the unit interval is an ordinary
// sample rather than an impossible one.
//
// **What a reduction over nothing answers.** Every reduction in this engine goes through one call
// that turns an empty selection into a named outcome carrying both counts, so a caller has one
// branch for it rather than one per operation, and no reduction ever returns a seed as though it
// were a measurement.

#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace
{

/// A descriptor for a small monochrome frame whose magnitudes mean `domain`.
[[nodiscard]] std::optional<an::ImageDescriptor> describeDomain(an::SampleType type, an::NumericDomain domain)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(8u, 4u, 1u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = type,
                                             .channelSchema = an::ChannelSchema::Mono,
                                             .colorSpace = an::ColorSpace::Unspecified,
                                             .numericDomain = domain,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    return an::ImageDescriptor::create(request);
}

} // namespace

TEST(NumericDomainTest, TwoOfTheFourMeaningsAreNotFractionsOfFullScaleAtAll)
{
    // The seed of one and zero assumes every image is the first meaning. These two are counterexamples
    // that a real frame has: an undebayered sensor frame holds the numbers the sensor reported, and a
    // calibrated one holds a physical quantity. Neither has a known upper bound at all, so an extreme
    // seeded from a literal is not conservative - it is a value that is simply not in the image.
    for (const an::NumericDomain domain : {an::NumericDomain::RawSensorCode, an::NumericDomain::PhysicalScaled})
    {
        const std::optional<an::ImageDescriptor> described = describeDomain(
            domain == an::NumericDomain::PhysicalScaled ? an::SampleType::Float32 : an::SampleType::UInt16, domain);
        ASSERT_TRUE(described.has_value()) << an::numericDomainName(domain);
        EXPECT_EQ(described->numericDomain(), domain) << an::numericDomainName(domain);
        EXPECT_NE(described->numericDomain(), an::NumericDomain::NormalizedLinear);
    }
}

TEST(NumericDomainTest, AnImageCarriesItsMeaningRatherThanLettingAReaderInferOneFromTheSamples)
{
    // The same representation under two meanings is two different images, and neither is the default
    // for the other. That is what makes "seed the extremes at zero and one" a statement about the
    // image rather than a harmless initialiser: it is only true of one of these.
    const std::optional<an::ImageDescriptor> normalized =
        describeDomain(an::SampleType::UInt16, an::NumericDomain::NormalizedLinear);
    const std::optional<an::ImageDescriptor> codes =
        describeDomain(an::SampleType::UInt16, an::NumericDomain::RawSensorCode);
    ASSERT_TRUE(normalized.has_value());
    ASSERT_TRUE(codes.has_value());
    EXPECT_FALSE(*normalized == *codes);
}

TEST(ValidityRangeTest, AStatedRangeIsTheOneTheImageStatesAndNotTheUnitInterval)
{
    // Where a range does come from, it is stated in full and carried, including ranges that have
    // nothing to do with zero and one. A rule that read a literal interval instead would call every
    // sample of a sensor frame meaningless.
    const an::ValidityDescriptorRequest request{
        .rule = an::ValidityRule::DomainRange, .lowestValid = -3000.0, .highestValid = 65535.0};
    const std::optional<an::ValidityDescriptor> described = an::ValidityDescriptor::create(request);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->lowestValid(), -3000.0);
    EXPECT_EQ(described->highestValid(), 65535.0);

    // Both ends are inside, which is what makes it a closed range rather than a half-open one.
    EXPECT_TRUE(an::sampleIsValidByValue(*described, -3000.0));
    EXPECT_TRUE(an::sampleIsValidByValue(*described, 65535.0));
    EXPECT_TRUE(an::sampleIsValidByValue(*described, 40000.0));
    EXPECT_FALSE(an::sampleIsValidByValue(*described, 65536.0));
}

TEST(ValidityRangeTest, EverySampleMeansSomethingUntilARuleSaysOtherwise)
{
    // The default rule claims every sample, so a value of two, or of minus one, is data rather than
    // something a reduction may skip on the grounds that it is outside an interval nobody stated.
    const an::ValidityDescriptor everything;
    EXPECT_EQ(everything.rule(), an::ValidityRule::AllValid);
    for (const double sample : {-1.0, 0.0, 0.5, 1.0, 2.0, 1.0e9})
    {
        EXPECT_TRUE(an::sampleIsValidByValue(everything, sample)) << sample;
    }
}

TEST(EmptyReductionTest, AReductionWithNothingLeftToReduceReportsAnOutcomeAndBothCounts)
{
    // A frame every sample of which was filtered out - a fully saturated flat under a range rule, a
    // fully masked light. The other engine's answer is its seed: a minimum of one and a maximum of
    // zero, indistinguishable from a measurement of a black frame.
    const an::ValidityCount filteredOut{.valid = 0u, .total = 9'000'000u};
    EXPECT_TRUE(filteredOut.empty());
    EXPECT_FALSE(filteredOut.complete());

    const an::Status refused = an::requireSelection(filteredOut, "reduceExtremes");
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::EmptySelection);

    // Both numbers arrive with it, because "four of nine million survived" and "none did" are
    // different situations and a caller keeping or discarding a frame acts differently on each.
    const an::StatusContextField* const valid = refused.findContext(an::ContextKey::Completed);
    const an::StatusContextField* const total = refused.findContext(an::ContextKey::Total);
    ASSERT_NE(valid, nullptr);
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(valid->count, 0u);
    EXPECT_EQ(total->count, 9'000'000u);
}

TEST(EmptyReductionTest, ASelectionOverNoSamplesAtAllIsTheSameOutcomeAsOneOverNoSurvivors)
{
    // Two ways to have nothing to reduce - an empty region and a region where nothing survived - and
    // one answer, so a caller has one branch rather than two.
    const an::Status noRegion = an::requireSelection(an::ValidityCount{.valid = 0u, .total = 0u}, "reduceMean");
    const an::Status noSurvivors = an::requireSelection(an::ValidityCount{.valid = 0u, .total = 64u}, "reduceMean");
    EXPECT_EQ(noRegion.code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(noSurvivors.code(), an::ErrorCode::EmptySelection);
}

TEST(EmptyReductionTest, OneSurvivingSampleIsAMeasurementAndNotAnEmptySelection)
{
    // The boundary the defect is on the wrong side of: a single surviving sample has a minimum and a
    // maximum, both equal to it, and that is a constant image rather than an error. What a
    // normalisation does with a range of zero is a decision for whoever owns the normalisation; that
    // it is reached at all is decided here.
    const an::ValidityCount barely{.valid = 1u, .total = 9'000'000u};
    EXPECT_FALSE(barely.empty());
    EXPECT_FALSE(barely.complete());
    EXPECT_TRUE(an::requireSelection(barely, "reduceExtremes").ok());
}

TEST(EmptyReductionTest, ARuleThatRejectsEveryNonFiniteValueLeavesAnAllNotANumberFrameWithNothing)
{
    // The frame that produces the defect's most confusing output: every sample not a number, which
    // the other engine's comparisons silently drop, leaving the seed behind as the answer. Here the
    // rule says which samples mean something and the count says how many did.
    const an::ValidityDescriptorRequest request{.rule = an::ValidityRule::FiniteOnly};
    const std::optional<an::ValidityDescriptor> finite = an::ValidityDescriptor::create(request);
    ASSERT_TRUE(finite.has_value());

    constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInfinite = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(an::sampleIsValidByValue(*finite, kNotANumber));
    EXPECT_FALSE(an::sampleIsValidByValue(*finite, kInfinite));
    EXPECT_FALSE(an::sampleIsValidByValue(*finite, -kInfinite));
    EXPECT_TRUE(an::sampleIsValidByValue(*finite, 0.0));

    std::uint64_t surviving = 0u;
    constexpr double kFrame[] = {kNotANumber, kNotANumber, kNotANumber, kNotANumber};
    for (const double sample : kFrame)
    {
        surviving += an::sampleIsValidByValue(*finite, sample) ? 1u : 0u;
    }
    const an::ValidityCount counted{.valid = surviving, .total = std::size(kFrame)};
    EXPECT_EQ(an::requireSelection(counted, "reduceExtremes").code(), an::ErrorCode::EmptySelection);
}
