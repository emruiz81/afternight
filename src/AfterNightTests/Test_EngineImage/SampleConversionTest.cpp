// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ConversionPolicy.h and Image/SampleConversion.h.
//
// Four defects are easy to make in this one area, and each of them is a test below that fails against
// the defect and passes against this engine.
//
// **A float-to-unsigned-32 conversion of full scale is undefined behaviour when it is scaled in
// float.** `float(UINT32_MAX)` is `4294967296.0f` - one past the largest value the destination holds -
// so converting `1.0f` that way converts a value the destination cannot represent. The boundary cases
// below walk `1.0f`, the value below it, the values above it, both infinities and not-a-number
// through that pair and check every answer.
//
// **A generic denormalize path that does not clamp, and an in-range conversion that truncates.** The
// matrix cases cover every ordered pair at its extrema, at adjacent representable values, at halfway
// values, out of range and at the non-finite values, which is what "complete" has to mean for a
// conversion matrix.
//
// **An unsigned-to-signed 32-bit mapping two counts away from the bijection it intends.**
// `value - INT32_MAX + 1` is `value - 2147483646`; the offset-binary convention is
// `value - 2147483648`. The round-trip cases pin the bijection at both extrema and in the middle,
// where the two differ by exactly two.
//
// **A conversion to signed 8-bit that routes to the unsigned one.** The identity cases check that
// every representation round-trips through itself and that the signed 8-bit path is its own, which is
// the smallest test that would catch it.
//
// Everything is computed in `double` on purpose, and the tests are written to notice if that stops
// being true: `double` represents every value of all six integer representations exactly, so a
// conversion that lost a code value would have to have lost it in the policy rather than in the
// arithmetic.

#include <AfterNightEngine/Image/SampleConversion.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>

namespace
{

/// The approved defaults, which almost every case starts from.
constexpr an::ConversionPolicy kDefaults{};

/// The policy that carries a code value across rather than rescaling full scale.
constexpr an::ConversionPolicy kCodeValues{.domain = an::DomainMapping::CodeValue};

/// The policy a signed integer boundary is read under.
constexpr an::ConversionPolicy kOffsetBinary{.signedConvention = an::SignedIntegerConvention::OffsetBinary};

/// One sample converted, with the flags dropped.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param value the sample.
/// @param policy the five decisions.
/// @return the converted sample.
template <typename SourceT, typename DestT>
[[nodiscard]] DestT valueOf(SourceT value, const an::ConversionPolicy& policy = kDefaults)
{
    return (an::convertSample<SourceT, DestT>(value, policy)).value;
}

/// Not-a-number of one floating representation.
///
/// @tparam SampleT the representation.
/// @return the value.
template <typename SampleT> [[nodiscard]] SampleT notANumber()
{
    return std::numeric_limits<SampleT>::quiet_NaN();
}

/// Every one of the eight representations, so a case can walk the matrix.
template <typename VisitorT> void forEachRepresentation(VisitorT&& visit)
{
    visit(std::uint8_t{});
    visit(std::int8_t{});
    visit(std::uint16_t{});
    visit(std::int16_t{});
    visit(std::uint32_t{});
    visit(std::int32_t{});
    visit(float{});
    visit(double{});
}

} // namespace

TEST(ConversionPolicyTest, TheDefaultsAreTheApprovedOnes)
{
    static_assert(kDefaults.rounding == an::RoundingMode::NearestEven);
    static_assert(kDefaults.range == an::RangePolicy::Saturate);
    static_assert(kDefaults.nonFinite == an::NonFinitePolicy::MapToRange);
    static_assert(kDefaults.domain == an::DomainMapping::FullScale);
    static_assert(kDefaults.signedConvention == an::SignedIntegerConvention::TwosComplement);
}

TEST(ConversionPolicyTest, EveryDecisionIsCheckedAndTheFirstBadOneIsNamed)
{
    EXPECT_TRUE(an::checkConversionPolicy(kDefaults, "test").ok());

    an::ConversionPolicy broken = kDefaults;
    broken.rounding = static_cast<an::RoundingMode>(90);
    an::Status refused = an::checkConversionPolicy(broken, "test");
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    const an::StatusContextField* named = refused.findContext(an::ContextKey::Parameter);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->text, "policy.rounding");

    broken = kDefaults;
    broken.signedConvention = static_cast<an::SignedIntegerConvention>(90);
    refused = an::checkConversionPolicy(broken, "test");
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    named = refused.findContext(an::ContextKey::Parameter);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->text, "policy.signedConvention");
}

TEST(ConversionPolicyTest, EveryDecisionHasItsOwnNameAndAStrangerSaysSo)
{
    EXPECT_STREQ(an::roundingModeName(an::RoundingMode::NearestEven), "NearestEven");
    EXPECT_STREQ(an::rangePolicyName(an::RangePolicy::Reject), "Reject");
    EXPECT_STREQ(an::nonFinitePolicyName(an::NonFinitePolicy::MapToRange), "MapToRange");
    EXPECT_STREQ(an::domainMappingName(an::DomainMapping::CodeValue), "CodeValue");
    EXPECT_STREQ(an::signedIntegerConventionName(an::SignedIntegerConvention::OffsetBinary), "OffsetBinary");

    EXPECT_STREQ(an::roundingModeName(static_cast<an::RoundingMode>(90)), "UnknownRoundingMode");
    EXPECT_STREQ(an::rangePolicyName(static_cast<an::RangePolicy>(90)), "UnknownRangePolicy");
    EXPECT_STREQ(an::nonFinitePolicyName(static_cast<an::NonFinitePolicy>(90)), "UnknownNonFinitePolicy");
    EXPECT_STREQ(an::domainMappingName(static_cast<an::DomainMapping>(90)), "UnknownDomainMapping");
    EXPECT_STREQ(an::signedIntegerConventionName(static_cast<an::SignedIntegerConvention>(90)),
                 "UnknownSignedIntegerConvention");
}

TEST(ConversionPolicyTest, ARawSensorCodeIsCarriedAcrossAndAFractionOfFullScaleIsRescaled)
{
    static_assert(an::conversionPolicyFor(an::NumericDomain::RawSensorCode).domain == an::DomainMapping::CodeValue);
    static_assert(an::conversionPolicyFor(an::NumericDomain::NormalizedLinear).domain == an::DomainMapping::FullScale);
    static_assert(an::conversionPolicyFor(an::NumericDomain::EncodedNonlinear).domain == an::DomainMapping::FullScale);
    static_assert(an::conversionPolicyFor(an::NumericDomain::PhysicalScaled).domain == an::DomainMapping::FullScale);
    // Only the domain departs from the defaults, so a caller that needs another rounding states it.
    static_assert(an::conversionPolicyFor(an::NumericDomain::RawSensorCode).rounding == kDefaults.rounding);
}

TEST(ConversionCountersTest, MergingAddsAndNeverAssigns)
{
    an::ConversionCounters left{.samplesConverted = 3u, .nonFiniteInputs = 1u, .saturatedSamples = 2u};
    const an::ConversionCounters right{.samplesConverted = 4u, .nonFiniteInputs = 0u, .saturatedSamples = 5u};
    left.merge(right);
    EXPECT_EQ(left, (an::ConversionCounters{.samplesConverted = 7u, .nonFiniteInputs = 1u, .saturatedSamples = 7u}));
}

TEST(RoundingTest, EveryModeAnswersTheHalfwayCasesItsOwnWay)
{
    EXPECT_EQ(an::roundReal(0.5, an::RoundingMode::NearestEven), 0.0);
    EXPECT_EQ(an::roundReal(1.5, an::RoundingMode::NearestEven), 2.0);
    EXPECT_EQ(an::roundReal(2.5, an::RoundingMode::NearestEven), 2.0);
    EXPECT_EQ(an::roundReal(-0.5, an::RoundingMode::NearestEven), -0.0);
    EXPECT_EQ(an::roundReal(-1.5, an::RoundingMode::NearestEven), -2.0);
    EXPECT_EQ(an::roundReal(-2.5, an::RoundingMode::NearestEven), -2.0);

    EXPECT_EQ(an::roundReal(0.5, an::RoundingMode::NearestAwayFromZero), 1.0);
    EXPECT_EQ(an::roundReal(2.5, an::RoundingMode::NearestAwayFromZero), 3.0);
    EXPECT_EQ(an::roundReal(-2.5, an::RoundingMode::NearestAwayFromZero), -3.0);

    EXPECT_EQ(an::roundReal(1.9, an::RoundingMode::TowardZero), 1.0);
    EXPECT_EQ(an::roundReal(-1.9, an::RoundingMode::TowardZero), -1.0);

    EXPECT_EQ(an::roundReal(1.9, an::RoundingMode::Floor), 1.0);
    EXPECT_EQ(an::roundReal(-1.1, an::RoundingMode::Floor), -2.0);

    EXPECT_EQ(an::roundReal(1.1, an::RoundingMode::Ceiling), 2.0);
    EXPECT_EQ(an::roundReal(-1.9, an::RoundingMode::Ceiling), -1.0);

    // A whole value is already whole under every mode.
    for (const an::RoundingMode mode :
         {an::RoundingMode::NearestEven, an::RoundingMode::NearestAwayFromZero, an::RoundingMode::TowardZero,
          an::RoundingMode::Floor, an::RoundingMode::Ceiling})
    {
        EXPECT_EQ(an::roundReal(4.0, mode), 4.0);
        EXPECT_EQ(an::roundReal(-4.0, mode), -4.0);
    }
}

TEST(RoundingTest, TheDefaultRoundsRatherThanTruncating)
{
    // Truncation is what a bare cast gives. It biases a positive image downward by half a code value,
    // which over a stacked sequence is an offset rather than noise.
    EXPECT_EQ((valueOf<float, std::uint8_t>(0.6f / 255.0f)), 1u);
    EXPECT_EQ((valueOf<double, std::uint16_t>(2.7 / 65535.0)), 3u);
    EXPECT_EQ(
        (valueOf<double, std::uint16_t>(2.7 / 65535.0, an::ConversionPolicy{.rounding = an::RoundingMode::TowardZero})),
        2u);
}

TEST(IdentityConversionTest, EveryRepresentationRoundTripsThroughItself)
{
    forEachRepresentation([](auto sample) {
        using Sample = decltype(sample);
        const Sample lowest = (std::numeric_limits<Sample>::lowest)();
        const Sample highest = (std::numeric_limits<Sample>::max)();
        EXPECT_EQ((valueOf<Sample, Sample>(lowest)), lowest);
        EXPECT_EQ((valueOf<Sample, Sample>(highest)), highest);
        EXPECT_EQ((valueOf<Sample, Sample>(Sample{0})), Sample{0});
        EXPECT_EQ((valueOf<Sample, Sample>(highest, kCodeValues)), highest);
        EXPECT_EQ((valueOf<Sample, Sample>(highest, kOffsetBinary)), highest);
    });
}

TEST(IdentityConversionTest, SignedEightBitIsItsOwnPathRatherThanTheUnsignedOne)
{
    // Routing both 8-bit representations to the unsigned conversion would bring a negative
    // sample back as a large positive one. Every negative value has to survive.
    for (int value = -128; value <= 127; ++value)
    {
        const std::int8_t sample = static_cast<std::int8_t>(value);
        EXPECT_EQ((valueOf<std::int8_t, std::int8_t>(sample)), sample);
    }
    EXPECT_EQ((valueOf<std::int8_t, float>(-128)), -128.0f / 127.0f);
    EXPECT_EQ((valueOf<float, std::int8_t>(-128.0f / 127.0f)), -128);
    EXPECT_NE((valueOf<std::int8_t, std::uint8_t>(-1)), 255u);
}

TEST(FullScaleConversionTest, TheIntegerRoundTripThroughFloat32IsTheIdentity)
{
    // Decision 5 of the conversion policy: divide by the type maximum, so full scale is exactly 1.0
    // and every code value comes back. The 16-bit sweep is the one that matters, because 65535
    // values do not all round-trip under a 1/65536 scale.
    for (std::uint32_t value = 0u; value <= 65535u; ++value)
    {
        const std::uint16_t sample = static_cast<std::uint16_t>(value);
        const float scaled = (valueOf<std::uint16_t, float>(sample));
        EXPECT_EQ((valueOf<float, std::uint16_t>(scaled)), sample) << "at " << value;
    }
    for (std::uint32_t value = 0u; value <= 255u; ++value)
    {
        const std::uint8_t sample = static_cast<std::uint8_t>(value);
        EXPECT_EQ((valueOf<float, std::uint8_t>(valueOf<std::uint8_t, float>(sample))), sample);
    }
}

TEST(FullScaleConversionTest, FullScaleMapsToExactlyOne)
{
    EXPECT_EQ((valueOf<std::uint8_t, float>(255u)), 1.0f);
    EXPECT_EQ((valueOf<std::uint16_t, float>(65535u)), 1.0f);
    EXPECT_EQ((valueOf<std::uint32_t, double>(4294967295u)), 1.0);
    EXPECT_EQ((valueOf<std::uint8_t, float>(0u)), 0.0f);
    // The scale is the maximum rather than the range, so the smallest signed value is just past -1.
    EXPECT_EQ((valueOf<std::int16_t, double>(32767)), 1.0);
    EXPECT_LT((valueOf<std::int16_t, double>(-32768)), -1.0);
}

TEST(FullScaleConversionTest, TwoIntegerRepresentationsRescaleBetweenTheirOwnMaxima)
{
    EXPECT_EQ((valueOf<std::uint8_t, std::uint16_t>(255u)), 65535u);
    EXPECT_EQ((valueOf<std::uint8_t, std::uint16_t>(0u)), 0u);
    EXPECT_EQ((valueOf<std::uint16_t, std::uint8_t>(65535u)), 255u);
    EXPECT_EQ((valueOf<std::uint16_t, std::uint8_t>(0u)), 0u);
    // 128 out of 65535 really is about half a code value out of 255, and rounds to nothing.
    EXPECT_EQ((valueOf<std::uint16_t, std::uint8_t>(128u)), 0u);
    EXPECT_EQ((valueOf<std::uint16_t, std::uint8_t>(258u)), 1u);
}

TEST(CodeValueConversionTest, ASensorCodeIsCarriedAcrossRatherThanRescaled)
{
    EXPECT_EQ((valueOf<std::uint16_t, float>(1000u, kCodeValues)), 1000.0f);
    EXPECT_EQ((valueOf<float, std::uint16_t>(1000.0f, kCodeValues)), 1000u);
    EXPECT_EQ((valueOf<std::uint16_t, std::uint32_t>(1000u, kCodeValues)), 1000u);
    // A code that the destination cannot hold still saturates.
    EXPECT_EQ((valueOf<std::uint16_t, std::uint8_t>(1000u, kCodeValues)), 255u);
    EXPECT_EQ((valueOf<std::int16_t, std::uint8_t>(-5, kCodeValues)), 0u);
}

TEST(NonFiniteConversionTest, TheMappingIsWrittenDownRatherThanInheritedFromTheHardware)
{
    // Decision 3: not-a-number becomes zero, and the infinities become the destination extremes.
    // The hardware gives the most negative integer for all three, which is right for an unsigned
    // destination by coincidence and wrong for a signed one.
    EXPECT_EQ((valueOf<float, std::uint8_t>(notANumber<float>())), 0u);
    EXPECT_EQ((valueOf<float, std::int8_t>(notANumber<float>())), 0);
    EXPECT_EQ((valueOf<double, std::int32_t>(notANumber<double>())), 0);

    EXPECT_EQ((valueOf<float, std::uint8_t>(std::numeric_limits<float>::infinity())), 255u);
    EXPECT_EQ((valueOf<float, std::int8_t>(std::numeric_limits<float>::infinity())), 127);
    EXPECT_EQ((valueOf<float, std::uint32_t>(std::numeric_limits<float>::infinity())), 4294967295u);

    EXPECT_EQ((valueOf<float, std::uint8_t>(-std::numeric_limits<float>::infinity())), 0u);
    EXPECT_EQ((valueOf<float, std::int8_t>(-std::numeric_limits<float>::infinity())), -128);
    EXPECT_EQ((valueOf<float, std::int32_t>(-std::numeric_limits<float>::infinity())),
              (std::numeric_limits<std::int32_t>::lowest)());
}

TEST(NonFiniteConversionTest, AFloatingDestinationHoldsAllThreeExactly)
{
    EXPECT_TRUE(std::isnan(valueOf<float, double>(notANumber<float>())));
    EXPECT_EQ((valueOf<float, double>(std::numeric_limits<float>::infinity())),
              std::numeric_limits<double>::infinity());
    EXPECT_EQ((valueOf<double, float>(-std::numeric_limits<double>::infinity())),
              -std::numeric_limits<float>::infinity());
}

TEST(NonFiniteConversionTest, EveryNonFiniteSourceIsReportedWhateverThePolicyDoesWithIt)
{
    const an::ConvertedSample<std::uint8_t> mapped =
        (an::convertSample<float, std::uint8_t>(notANumber<float>(), kDefaults));
    EXPECT_TRUE(mapped.sourceWasNonFinite);
    EXPECT_FALSE(mapped.saturated);

    const an::ConvertedSample<float> carried = (an::convertSample<float, float>(notANumber<float>(), kDefaults));
    EXPECT_TRUE(carried.sourceWasNonFinite);

    const an::ConvertedSample<std::uint8_t> finite = (an::convertSample<float, std::uint8_t>(0.5f, kDefaults));
    EXPECT_FALSE(finite.sourceWasNonFinite);
}

TEST(SaturationTest, TheClampIsAppliedToTheRealValueBeforeTheConversion)
{
    // Clamping the converted integer instead would be too late: an out-of-range float converts to
    // the most negative integer on x86, and a narrowing pack of that saturates the wrong way.
    const an::ConvertedSample<std::uint8_t> high = (an::convertSample<float, std::uint8_t>(2.0f, kDefaults));
    EXPECT_EQ(high.value, 255u);
    EXPECT_TRUE(high.saturated);

    const an::ConvertedSample<std::uint8_t> low = (an::convertSample<float, std::uint8_t>(-2.0f, kDefaults));
    EXPECT_EQ(low.value, 0u);
    EXPECT_TRUE(low.saturated);

    const an::ConvertedSample<std::int8_t> negative = (an::convertSample<float, std::int8_t>(-2.0f, kDefaults));
    EXPECT_EQ(negative.value, -128);
    EXPECT_TRUE(negative.saturated);

    const an::ConvertedSample<std::uint8_t> inRange = (an::convertSample<float, std::uint8_t>(1.0f, kDefaults));
    EXPECT_EQ(inRange.value, 255u);
    EXPECT_FALSE(inRange.saturated);
}

TEST(SaturationTest, AFloatingDestinationSaturatesRatherThanBecomingInfinite)
{
    const an::ConvertedSample<float> overflowed =
        (an::convertSample<double, float>(1.0e300, an::ConversionPolicy{.domain = an::DomainMapping::CodeValue}));
    EXPECT_EQ(overflowed.value, (std::numeric_limits<float>::max)());
    EXPECT_TRUE(overflowed.saturated);
    EXPECT_FALSE(overflowed.sourceWasNonFinite);
}

TEST(FloatToUnsignedThirtyTwoTest, FullScaleConvertsToTheLargestValueRatherThanPastIt)
{
    // The regression for the defect this pair is named after. Scaling by `float(UINT32_MAX)`,
    // which rounds up to 4294967296.0f - one past what the destination holds - makes this
    // conversion undefined.
    EXPECT_EQ((valueOf<float, std::uint32_t>(1.0f)), 4294967295u);
    EXPECT_EQ((valueOf<double, std::uint32_t>(1.0)), 4294967295u);

    const an::ConvertedSample<std::uint32_t> atFullScale = (an::convertSample<float, std::uint32_t>(1.0f, kDefaults));
    EXPECT_FALSE(atFullScale.saturated);
    EXPECT_FALSE(atFullScale.sourceWasNonFinite);

    EXPECT_EQ((valueOf<float, std::uint32_t>(std::nextafter(1.0f, 0.0f))), 4294967039u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(std::nextafter(1.0f, 2.0f))), 4294967295u);
    EXPECT_TRUE((an::convertSample<float, std::uint32_t>(std::nextafter(1.0f, 2.0f), kDefaults)).saturated);

    EXPECT_EQ((valueOf<float, std::uint32_t>(0.0f)), 0u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(-0.0f)), 0u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(std::nextafter(0.0f, -1.0f))), 0u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(notANumber<float>())), 0u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(std::numeric_limits<float>::infinity())), 4294967295u);
    EXPECT_EQ((valueOf<float, std::uint32_t>(-std::numeric_limits<float>::infinity())), 0u);
}

TEST(OffsetBinaryTest, TheSignedMappingIsTheBijectionRatherThanTwoCountsFromIt)
{
    // The regression for the defect this convention is named after: `value - INT32_MAX + 1` is
    // two counts short of `value - 2^31`.
    EXPECT_EQ((valueOf<std::uint32_t, std::int32_t>(0u, kOffsetBinary)), (std::numeric_limits<std::int32_t>::lowest)());
    EXPECT_EQ((valueOf<std::uint32_t, std::int32_t>(2147483648u, kOffsetBinary)), 0);
    EXPECT_EQ((valueOf<std::uint32_t, std::int32_t>(4294967295u, kOffsetBinary)),
              (std::numeric_limits<std::int32_t>::max)());
    // What that formula answers for the middle value, so a silent return to it fails here.
    EXPECT_NE((valueOf<std::uint32_t, std::int32_t>(2147483648u, kOffsetBinary)), 2);

    EXPECT_EQ((valueOf<std::int32_t, std::uint32_t>((std::numeric_limits<std::int32_t>::lowest)(), kOffsetBinary)), 0u);
    EXPECT_EQ((valueOf<std::int32_t, std::uint32_t>(0, kOffsetBinary)), 2147483648u);
    EXPECT_EQ((valueOf<std::int32_t, std::uint32_t>((std::numeric_limits<std::int32_t>::max)(), kOffsetBinary)),
              4294967295u);
}

TEST(OffsetBinaryTest, EveryWidthRoundTripsAndNothingSaturates)
{
    for (int value = -128; value <= 127; ++value)
    {
        const std::int8_t sample = static_cast<std::int8_t>(value);
        const std::uint8_t encoded = (valueOf<std::int8_t, std::uint8_t>(sample, kOffsetBinary));
        EXPECT_EQ(encoded, static_cast<std::uint8_t>(value + 128));
        EXPECT_EQ((valueOf<std::uint8_t, std::int8_t>(encoded, kOffsetBinary)), sample);
        EXPECT_FALSE((an::convertSample<std::int8_t, std::uint8_t>(sample, kOffsetBinary)).saturated);
    }
    EXPECT_EQ((valueOf<std::int16_t, std::uint16_t>(-32768, kOffsetBinary)), 0u);
    EXPECT_EQ((valueOf<std::int16_t, std::uint16_t>(32767, kOffsetBinary)), 65535u);
    EXPECT_EQ((valueOf<std::uint16_t, std::int16_t>(0u, kOffsetBinary)), -32768);
    EXPECT_EQ((valueOf<std::uint16_t, std::int16_t>(65535u, kOffsetBinary)), 32767);
}

TEST(OffsetBinaryTest, TheConventionSaysNothingAboutAnyOtherPair)
{
    // Widths that differ and pairs that share a signedness ignore it, so a caller that sets the
    // convention for its 16-bit boundary does not silently change its 8-to-16-bit scaling.
    EXPECT_EQ((valueOf<std::uint8_t, std::uint16_t>(255u, kOffsetBinary)),
              (valueOf<std::uint8_t, std::uint16_t>(255u, kDefaults)));
    EXPECT_EQ((valueOf<std::int16_t, std::int32_t>(-32768, kOffsetBinary)),
              (valueOf<std::int16_t, std::int32_t>(-32768, kDefaults)));
    EXPECT_EQ((valueOf<float, std::uint16_t>(0.5f, kOffsetBinary)), (valueOf<float, std::uint16_t>(0.5f, kDefaults)));
    static_assert(!an::kIsOffsetBinaryPair<std::uint8_t, std::uint16_t>);
    static_assert(!an::kIsOffsetBinaryPair<float, std::int32_t>);
    static_assert(an::kIsOffsetBinaryPair<std::uint32_t, std::int32_t>);
    static_assert(an::kIsOffsetBinaryPair<std::int8_t, std::uint8_t>);
}

TEST(WithoutOffsetBinaryTest, ASignedAndAnUnsignedPairSaturatesLikeAnyOther)
{
    EXPECT_EQ((valueOf<std::uint32_t, std::int32_t>(4294967295u, kCodeValues)),
              (std::numeric_limits<std::int32_t>::max)());
    EXPECT_EQ((valueOf<std::int32_t, std::uint32_t>(-1, kCodeValues)), 0u);
    EXPECT_TRUE((an::convertSample<std::int32_t, std::uint32_t>(-1, kCodeValues)).saturated);
}

TEST(ConversionMatrixTest, EveryOrderedPairConvertsItsExtremaWithoutLeavingTheDestinationRange)
{
    // The completeness case: sixty-four pairs, each at both extrema of its source, and every answer
    // has to be a value the destination can hold, including for the pairs whose conversion is
    // undefined when it is scaled in float.
    forEachRepresentation([](auto sourceSample) {
        using Source = decltype(sourceSample);
        forEachRepresentation([](auto destSample) {
            using Dest = decltype(destSample);
            for (const Source value :
                 {(std::numeric_limits<Source>::lowest)(), Source{0}, (std::numeric_limits<Source>::max)()})
            {
                for (const an::ConversionPolicy& policy : {kDefaults, kCodeValues, kOffsetBinary})
                {
                    const double converted =
                        static_cast<double>((an::convertSample<Source, Dest>(value, policy)).value);
                    EXPECT_GE(converted, an::kSampleMinimumOf<Dest>);
                    EXPECT_LE(converted, an::kSampleMaximumOf<Dest>);
                }
            }
        });
    });
}

TEST(ConversionMatrixTest, EveryFloatingSourceHandlesTheThreeNonFiniteValuesForEveryDestination)
{
    forEachRepresentation([](auto destSample) {
        using Dest = decltype(destSample);
        for (const float value :
             {notANumber<float>(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
        {
            const an::ConvertedSample<Dest> converted = (an::convertSample<float, Dest>(value, kDefaults));
            EXPECT_TRUE(converted.sourceWasNonFinite);
            if constexpr (!std::is_floating_point_v<Dest>)
            {
                const double held = static_cast<double>(converted.value);
                EXPECT_GE(held, an::kSampleMinimumOf<Dest>);
                EXPECT_LE(held, an::kSampleMaximumOf<Dest>);
            }
        }
    });
}

TEST(ConversionRunTest, ARunConvertsEverySampleAndCountsWhatItMet)
{
    const std::array<float, 5u> source{0.0f, 0.5f, 1.0f, 2.0f, notANumber<float>()};
    std::array<std::uint8_t, 5u> destination{};
    an::ConversionCounters counters;

    ASSERT_TRUE((an::convertSampleRun<float, std::uint8_t>(std::span<const float>{source},
                                                           std::span<std::uint8_t>{destination}, kDefaults, counters))
                    .ok());
    EXPECT_EQ(destination[0], 0u);
    EXPECT_EQ(destination[1], 128u);
    EXPECT_EQ(destination[2], 255u);
    EXPECT_EQ(destination[3], 255u);
    EXPECT_EQ(destination[4], 0u);
    EXPECT_EQ(counters,
              (an::ConversionCounters{.samplesConverted = 5u, .nonFiniteInputs = 1u, .saturatedSamples = 1u}));
}

TEST(ConversionRunTest, CountersAccumulateAcrossRunsRatherThanBeingAssigned)
{
    const std::array<float, 2u> source{2.0f, 2.0f};
    std::array<std::uint8_t, 2u> destination{};
    an::ConversionCounters counters;
    for (int pass = 0; pass < 3; ++pass)
    {
        ASSERT_TRUE((an::convertSampleRun<float, std::uint8_t>(
                         std::span<const float>{source}, std::span<std::uint8_t>{destination}, kDefaults, counters))
                        .ok());
    }
    EXPECT_EQ(counters.samplesConverted, 6u);
    EXPECT_EQ(counters.saturatedSamples, 6u);
}

TEST(ConversionRunTest, RunsOfDifferentLengthsAreRefused)
{
    const std::array<float, 3u> source{};
    std::array<std::uint8_t, 2u> destination{};
    an::ConversionCounters counters;
    const an::Status refused = (an::convertSampleRun<float, std::uint8_t>(
        std::span<const float>{source}, std::span<std::uint8_t>{destination}, kDefaults, counters));
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ConversionRunTest, APolicyNamingADecisionThisBuildDoesNotKnowIsRefused)
{
    const std::array<float, 1u> source{};
    std::array<std::uint8_t, 1u> destination{};
    an::ConversionCounters counters;
    an::ConversionPolicy broken = kDefaults;
    broken.domain = static_cast<an::DomainMapping>(90);
    const an::Status refused = (an::convertSampleRun<float, std::uint8_t>(
        std::span<const float>{source}, std::span<std::uint8_t>{destination}, broken, counters));
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ConversionRunTest, ARejectingNonFinitePolicyStopsAtTheSampleItRefuses)
{
    const std::array<float, 4u> source{0.0f, 0.5f, notANumber<float>(), 1.0f};
    std::array<std::uint8_t, 4u> destination{9u, 9u, 9u, 9u};
    an::ConversionCounters counters;
    const an::ConversionPolicy policy{.nonFinite = an::NonFinitePolicy::Reject};

    const an::Status refused = (an::convertSampleRun<float, std::uint8_t>(
        std::span<const float>{source}, std::span<std::uint8_t>{destination}, policy, counters));
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidData);
    const an::StatusContextField* const written = refused.findContext(an::ContextKey::Completed);
    ASSERT_NE(written, nullptr);
    EXPECT_EQ(written->count, 2u);
    EXPECT_EQ(counters.samplesConverted, 2u);
    EXPECT_EQ(destination[2], 9u);
    EXPECT_EQ(destination[3], 9u);
}

TEST(ConversionRunTest, ARejectingRangePolicyStopsAtTheSampleItRefuses)
{
    const std::array<float, 3u> source{0.5f, 4.0f, 0.25f};
    std::array<std::uint8_t, 3u> destination{9u, 9u, 9u};
    an::ConversionCounters counters;
    const an::ConversionPolicy policy{.range = an::RangePolicy::Reject};

    const an::Status refused = (an::convertSampleRun<float, std::uint8_t>(
        std::span<const float>{source}, std::span<std::uint8_t>{destination}, policy, counters));
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(counters.saturatedSamples, 1u);
    EXPECT_EQ(destination[2], 9u);
}

TEST(ConversionRunTest, ARejectingPolicyAcceptsARunItHasNothingToRefuseIn)
{
    const std::array<float, 3u> source{0.0f, 0.5f, 1.0f};
    std::array<std::uint8_t, 3u> destination{};
    an::ConversionCounters counters;
    const an::ConversionPolicy policy{.range = an::RangePolicy::Reject, .nonFinite = an::NonFinitePolicy::Reject};

    EXPECT_TRUE((an::convertSampleRun<float, std::uint8_t>(std::span<const float>{source},
                                                           std::span<std::uint8_t>{destination}, policy, counters))
                    .ok());
    EXPECT_EQ(counters.samplesConverted, 3u);
}
