// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the public stacking model: the closed vocabularies, the compatibility table
// over every supported mode, the per-family parameter rules, the declared effect and the typed
// refusals a planner receives.

#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackModelError.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRequest.h>
#include <AfterNightEngine/Algorithms/Stacking/StackOutput.h>
#include <AfterNightEngine/Algorithms/Stacking/StackNormalizationParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Algorithms/Stacking/StackCombination.h>

#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// A descriptor for a 64 by 48 frame of the stated representation, schema and domain.
[[nodiscard]] an::ImageDescriptor makeDescriptor(an::SampleType sampleType = an::SampleType::Float32,
                                                 an::ChannelSchema schema = an::ChannelSchema::Mono,
                                                 an::NumericDomain domain = an::NumericDomain::NormalizedLinear)
{
    const std::uint32_t channels = an::channelCountOf(schema).value_or(1u);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(64u, 48u, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    an::ColorSpace colorSpace = an::ColorSpace::Unspecified;
    if (schema == an::ChannelSchema::Rgb && domain != an::NumericDomain::RawSensorCode)
    {
        colorSpace =
            domain == an::NumericDomain::EncodedNonlinear ? an::ColorSpace::DisplayRgb : an::ColorSpace::LinearRgb;
    }
    const an::ImageDescriptorRequest request{.extent = *extent,
                                             .sampleType = sampleType,
                                             .channelSchema = schema,
                                             .colorSpace = colorSpace,
                                             .numericDomain = domain,
                                             .cfa = {},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return *descriptor;
}

/// A plain five-frame average of Float32 monochrome frames, which every rule admits.
[[nodiscard]] an::StackRequest baseRequest(std::uint32_t frames = 5u,
                                           const an::ImageDescriptor& descriptor = makeDescriptor())
{
    return an::StackRequest{.input = an::StackInputDescription{.frameCount = frames, .descriptor = descriptor}};
}

/// The first count-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::uint64_t> countField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Count)
        {
            return field.count;
        }
    }
    return std::nullopt;
}

/// The first text-valued context field under a key, if the failure carries one.
[[nodiscard]] std::optional<std::string> textField(const an::Status& status, an::ContextKey key)
{
    if (status.detail() == nullptr)
    {
        return std::nullopt;
    }
    for (const an::StatusContextField& field : status.detail()->context)
    {
        if (field.key == key && field.kind == an::ContextValueKind::Text)
        {
            return field.text;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool messageNames(const an::Status& status, std::string_view name)
{
    return std::string_view(status.message()).find(name) != std::string_view::npos;
}

// The model is a constant expression end to end: a descriptor, a request, its check, the model
// and the facts read from it are all evaluated here at compile time.
constexpr std::optional<an::ImageExtent> kConstantExtent =
    an::ImageExtent::create(16u, 16u, 1u, an::defaultEngineLimits());
static_assert(kConstantExtent.has_value());
constexpr std::optional<an::ImageDescriptor> kConstantDescriptor =
    an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *kConstantExtent,
                                                           .sampleType = an::SampleType::UInt16,
                                                           .channelSchema = an::ChannelSchema::Mono,
                                                           .colorSpace = an::ColorSpace::Unspecified,
                                                           .numericDomain = an::NumericDomain::NormalizedLinear,
                                                           .cfa = {},
                                                           .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                           .pixelAspectRatio = 1.0});
static_assert(kConstantDescriptor.has_value());
constexpr an::StackRequest kConstantRequest{
    .input = an::StackInputDescription{.frameCount = 7u, .descriptor = *kConstantDescriptor},
    .combination = an::StackCombination::Average,
    .rejection = an::StackRejectionRequest{.method = an::StackRejection::WinsorizedSigmaClipping},
    .weighting = an::StackWeightingRequest{.method = an::StackWeighting::Explicit},
    .output = an::StackOutputRequest{.sampleType = an::SampleType::Float64,
                                     .rejectionMaps = an::RejectionMapForm::LowAndHigh}};
static_assert(an::StackModel::check(kConstantRequest) == an::StackModelError::None);
constexpr std::optional<an::StackModel> kConstantModel = an::StackModel::create(kConstantRequest);
static_assert(kConstantModel.has_value());
static_assert(kConstantModel->usesRejection());
static_assert(kConstantModel->usesFrameWeights());
static_assert(!kConstantModel->usesLocalWeights());
static_assert(kConstantModel->producesRejectionMaps());
static_assert(kConstantModel->outputDescriptor().sampleType() == an::SampleType::Float64);
static_assert(kConstantModel->outputDescriptor().extent() == kConstantExtent);
static_assert(kConstantModel->accumulationPrecision() == an::ComputePrecision::Float64);
static_assert(an::checkComputePrecision(an::SampleType::UInt16, kConstantModel->outputContract(),
                                        kConstantModel->accumulationPrecision(),
                                        kConstantModel->accumulationJustification()) == an::PrecisionError::None);
static_assert(an::stackCombinationCapabilities(an::StackCombination::Average).admitsRejection);
static_assert(!an::stackCombinationCapabilities(an::StackCombination::Median).admitsWeighting);
static_assert(
    an::stackCombinationCapabilities(an::StackCombination::AutoAdaptiveWeightedAverage).requiresNormalizedLinearDomain);
static_assert(an::checkOperationEffect(an::kStackOperationEffect) == an::OperationEffectError::None);
static_assert(an::stackModelErrorCode(an::StackModelError::None) == an::ErrorCode::Ok);
static_assert(an::stackModelErrorCode(an::StackModelError::TooManyFrames) == an::ErrorCode::LimitExceeded);

TEST(StackingModelTest, EveryCombinationStatesItsRow)
{
    struct Row
    {
        an::StackCombination combination;
        const char* name;
        an::StackCombinationCapabilities capabilities;
    };
    const std::array<Row, 6u> rows{{
        {an::StackCombination::Average, "Average", {true, true, false}},
        {an::StackCombination::Sum, "Sum", {false, true, false}},
        {an::StackCombination::Median, "Median", {false, false, false}},
        {an::StackCombination::Minimum, "Minimum", {false, false, false}},
        {an::StackCombination::Maximum, "Maximum", {false, false, false}},
        {an::StackCombination::AutoAdaptiveWeightedAverage, "AutoAdaptiveWeightedAverage", {false, false, true}},
    }};
    for (const Row& row : rows)
    {
        EXPECT_TRUE(an::isStackCombination(row.combination));
        EXPECT_STREQ(an::stackCombinationName(row.combination), row.name);
        EXPECT_EQ(an::stackCombinationCapabilities(row.combination), row.capabilities) << row.name;
    }

    const auto unknown = static_cast<an::StackCombination>(250);
    EXPECT_FALSE(an::isStackCombination(unknown));
    EXPECT_STREQ(an::stackCombinationName(unknown), "UnknownStackCombination");
    EXPECT_EQ(an::stackCombinationCapabilities(unknown), an::StackCombinationCapabilities{});
}

TEST(StackingModelTest, TheThirteenStackingModesAllDescribeAStack)
{
    // The thirteen stacking modes are one combination with one rejection each, and every pair
    // describes a stack of five frames.
    struct Mode
    {
        an::StackCombination combination;
        an::StackRejection rejection;
    };
    const std::array<Mode, 13u> modes{{
        {an::StackCombination::Maximum, an::StackRejection::None},
        {an::StackCombination::Minimum, an::StackRejection::None},
        {an::StackCombination::Average, an::StackRejection::None},
        {an::StackCombination::Sum, an::StackRejection::None},
        {an::StackCombination::Median, an::StackRejection::None},
        {an::StackCombination::AutoAdaptiveWeightedAverage, an::StackRejection::None},
        {an::StackCombination::Average, an::StackRejection::SigmaClipping},
        {an::StackCombination::Average, an::StackRejection::MadSigmaClipping},
        {an::StackCombination::Average, an::StackRejection::MedianSigmaReplacement},
        {an::StackCombination::Average, an::StackRejection::WinsorizedSigmaClipping},
        {an::StackCombination::Average, an::StackRejection::LinearFitClipping},
        {an::StackCombination::Average, an::StackRejection::PercentileClipping},
        {an::StackCombination::Average, an::StackRejection::GeneralizedEsdTest},
    }};
    for (const Mode& mode : modes)
    {
        an::StackRequest request = baseRequest();
        request.combination = mode.combination;
        request.rejection.method = mode.rejection;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
            << an::stackCombinationName(mode.combination) << " with " << an::stackRejectionName(mode.rejection);
        const std::optional<an::StackModel> model = an::StackModel::create(request);
        ASSERT_TRUE(model.has_value());
        EXPECT_EQ(model->usesRejection(), mode.rejection != an::StackRejection::None);
        EXPECT_FALSE(model->usesFrameWeights());
        EXPECT_EQ(model->capabilities(), an::stackCombinationCapabilities(mode.combination));
        EXPECT_EQ(model->request(), request);
    }

    // Every rejection family reads exactly one parameter record, and the two replacing families
    // are the ones that hand every sample on.
    for (std::uint8_t value = 0u; an::isStackRejection(static_cast<an::StackRejection>(value)); ++value)
    {
        const auto rejection = static_cast<an::StackRejection>(value);
        const int records = (an::stackRejectionUsesSigmaThresholds(rejection) ? 1 : 0) +
                            (an::stackRejectionUsesMedianFractions(rejection) ? 1 : 0) +
                            (an::stackRejectionUsesGesdtParameters(rejection) ? 1 : 0);
        EXPECT_EQ(records, rejection == an::StackRejection::None ? 0 : 1) << an::stackRejectionName(rejection);
    }
    EXPECT_TRUE(an::stackRejectionReplacesSamples(an::StackRejection::MedianSigmaReplacement));
    EXPECT_TRUE(an::stackRejectionReplacesSamples(an::StackRejection::WinsorizedSigmaClipping));
    EXPECT_FALSE(an::stackRejectionReplacesSamples(an::StackRejection::SigmaClipping));
    EXPECT_FALSE(an::stackRejectionReplacesSamples(an::StackRejection::GeneralizedEsdTest));
    EXPECT_FALSE(an::stackRejectionReplacesSamples(an::StackRejection::None));

    // Every weighting source, every star-count scaling and every normalization scope describes a
    // stack on the average.
    for (std::uint8_t value = 0u; an::isStackWeighting(static_cast<an::StackWeighting>(value)); ++value)
    {
        an::StackRequest request = baseRequest();
        request.weighting.method = static_cast<an::StackWeighting>(value);
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
            << an::stackWeightingName(request.weighting.method);
        EXPECT_EQ(an::StackModel::create(request)->usesFrameWeights(),
                  request.weighting.method != an::StackWeighting::Uniform);
    }
    for (std::uint8_t value = 0u; an::isStarCountScaling(static_cast<an::StarCountScaling>(value)); ++value)
    {
        an::StackRequest request = baseRequest();
        request.weighting.method = an::StackWeighting::StarCount;
        request.weighting.starCount.scaling = static_cast<an::StarCountScaling>(value);
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
            << an::starCountScalingName(request.weighting.starCount.scaling);
    }
    for (std::uint8_t value = 0u; an::isStackNormalizationScope(static_cast<an::StackNormalizationScope>(value));
         ++value)
    {
        an::StackRequest request = baseRequest();
        request.normalization.scope = static_cast<an::StackNormalizationScope>(value);
        if (an::stackNormalizationScopeUsesGlobalMethod(request.normalization.scope))
        {
            request.normalization.globalMethod = an::NormalizationMethod::AdditiveScaling;
        }
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
            << an::stackNormalizationScopeName(request.normalization.scope);
    }
}

TEST(StackingModelTest, WhatACombinationCannotUseIsRefusedByName)
{
    const std::array<an::StackCombination, 5u> noRejection{an::StackCombination::Sum, an::StackCombination::Median,
                                                           an::StackCombination::Minimum, an::StackCombination::Maximum,
                                                           an::StackCombination::AutoAdaptiveWeightedAverage};
    for (const an::StackCombination combination : noRejection)
    {
        an::StackRequest request = baseRequest();
        request.combination = combination;
        request.rejection.method = an::StackRejection::SigmaClipping;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::RejectionNotAdmitted)
            << an::stackCombinationName(combination);
        EXPECT_FALSE(an::StackModel::create(request).has_value());
    }

    const std::array<an::StackCombination, 4u> noWeights{an::StackCombination::Median, an::StackCombination::Minimum,
                                                         an::StackCombination::Maximum,
                                                         an::StackCombination::AutoAdaptiveWeightedAverage};
    for (const an::StackCombination combination : noWeights)
    {
        an::StackRequest request = baseRequest();
        request.combination = combination;
        request.weighting.method = an::StackWeighting::Explicit;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::WeightingNotAdmitted)
            << an::stackCombinationName(combination);
        request.weighting.method = an::StackWeighting::Noise;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::WeightingNotAdmitted)
            << an::stackCombinationName(combination);
        request.weighting.method = an::StackWeighting::Uniform;
        request.localWeighting.enabled = true;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::LocalWeightingNotAdmitted)
            << an::stackCombinationName(combination);
    }

    // The sum weighs but does not reject.
    an::StackRequest sum = baseRequest();
    sum.combination = an::StackCombination::Sum;
    sum.weighting.method = an::StackWeighting::Explicit;
    sum.localWeighting.enabled = true;
    EXPECT_EQ(an::StackModel::check(sum), an::StackModelError::None);
    EXPECT_TRUE(an::StackModel::create(sum)->usesLocalWeights());

    // The auto-adaptive mean needs a normalized linear input; the average takes any domain.
    for (const an::NumericDomain domain :
         {an::NumericDomain::RawSensorCode, an::NumericDomain::EncodedNonlinear, an::NumericDomain::PhysicalScaled})
    {
        const an::SampleType sampleType =
            domain == an::NumericDomain::RawSensorCode ? an::SampleType::UInt16 : an::SampleType::Float32;
        an::StackRequest request = baseRequest(5u, makeDescriptor(sampleType, an::ChannelSchema::Mono, domain));
        request.combination = an::StackCombination::AutoAdaptiveWeightedAverage;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::CombinationNeedsNormalizedLinearDomain)
            << an::numericDomainName(domain);
        request.combination = an::StackCombination::Average;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None) << an::numericDomainName(domain);
    }
    an::StackRequest adaptive = baseRequest();
    adaptive.combination = an::StackCombination::AutoAdaptiveWeightedAverage;
    EXPECT_EQ(an::StackModel::check(adaptive), an::StackModelError::None);

    // Rejection maps count rejections, so they need a rejection to count.
    an::StackRequest maps = baseRequest();
    maps.output.rejectionMaps = an::RejectionMapForm::Merged;
    EXPECT_EQ(an::StackModel::check(maps), an::StackModelError::RejectionMapsWithoutRejection);
    EXPECT_FALSE(an::StackModel::create(maps).has_value());
    maps.rejection.method = an::StackRejection::MadSigmaClipping;
    EXPECT_EQ(an::StackModel::check(maps), an::StackModelError::None);
    EXPECT_TRUE(an::StackModel::create(maps)->producesRejectionMaps());
    an::StackRequest noMaps = baseRequest();
    noMaps.rejection.method = an::StackRejection::MadSigmaClipping;
    EXPECT_FALSE(an::StackModel::create(noMaps)->producesRejectionMaps());

    // Per-channel width weighting is a decision about the width source alone.
    for (const an::StackWeighting method :
         {an::StackWeighting::Uniform, an::StackWeighting::Explicit, an::StackWeighting::Noise,
          an::StackWeighting::StarCount, an::StackWeighting::ImageCount, an::StackWeighting::Photometric,
          an::StackWeighting::Exposure, an::StackWeighting::SignalToNoise})
    {
        an::StackRequest request = baseRequest();
        request.weighting.method = method;
        request.weighting.perChannelFwhm = true;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::PerChannelFwhmWithoutFwhmWeighting)
            << an::stackWeightingName(method);
    }
    an::StackRequest width = baseRequest();
    width.weighting.method = an::StackWeighting::Fwhm;
    width.weighting.perChannelFwhm = true;
    EXPECT_EQ(an::StackModel::check(width), an::StackModelError::None);
}

TEST(StackingModelTest, TheSelectedRejectionFamilyAloneIsValidated)
{
    const std::array<an::StackRejection, 5u> sigmaFamilies{
        an::StackRejection::SigmaClipping, an::StackRejection::MadSigmaClipping,
        an::StackRejection::MedianSigmaReplacement, an::StackRejection::WinsorizedSigmaClipping,
        an::StackRejection::LinearFitClipping};
    for (const an::StackRejection family : sigmaFamilies)
    {
        an::StackRequest request = baseRequest();
        request.rejection.method = family;
        for (const double bad : {0.0, -1.0, kNaN, kInfinity})
        {
            request.rejection.sigma = {.low = bad, .high = an::kDefaultSigmaClipThreshold};
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::SigmaThresholdNotPositive)
                << an::stackRejectionName(family) << " low " << bad;
            request.rejection.sigma = {.low = an::kDefaultSigmaClipThreshold, .high = bad};
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::SigmaThresholdNotPositive)
                << an::stackRejectionName(family) << " high " << bad;
        }
        request.rejection.sigma = {.low = 1.5, .high = 3.0};
        // The records the family does not read are inert, however malformed.
        request.rejection.medianFractions = {.low = -5.0, .high = kNaN};
        request.rejection.gesdt = {.maximumOutlierFraction = 7.0, .significance = -1.0};
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None) << an::stackRejectionName(family);
    }

    an::StackRequest percentile = baseRequest();
    percentile.rejection.method = an::StackRejection::PercentileClipping;
    for (const double bad : {0.0, -0.2, kNaN, kInfinity})
    {
        percentile.rejection.medianFractions = {.low = bad, .high = 0.1};
        EXPECT_EQ(an::StackModel::check(percentile), an::StackModelError::MedianFractionNotPositive) << bad;
        percentile.rejection.medianFractions = {.low = 0.2, .high = bad};
        EXPECT_EQ(an::StackModel::check(percentile), an::StackModelError::MedianFractionNotPositive) << bad;
    }
    // A fraction above one is a legitimate, if loose, distance from the median.
    percentile.rejection.medianFractions = {.low = 2.5, .high = 1.0};
    percentile.rejection.sigma = {.low = -1.0, .high = kNaN};
    EXPECT_EQ(an::StackModel::check(percentile), an::StackModelError::None);

    an::StackRequest gesdt = baseRequest();
    gesdt.rejection.method = an::StackRejection::GeneralizedEsdTest;
    for (const double bad : {0.0, 1.0, -0.5, 1.5, kNaN, kInfinity})
    {
        gesdt.rejection.gesdt = {.maximumOutlierFraction = bad, .significance = 0.05};
        EXPECT_EQ(an::StackModel::check(gesdt), an::StackModelError::GesdtOutlierFractionOutOfRange) << bad;
        gesdt.rejection.gesdt = {.maximumOutlierFraction = 0.3, .significance = bad};
        EXPECT_EQ(an::StackModel::check(gesdt), an::StackModelError::GesdtSignificanceOutOfRange) << bad;
    }
    gesdt.rejection.gesdt = {.maximumOutlierFraction = 0.5, .significance = 0.01};
    gesdt.rejection.sigma = {.low = 0.0, .high = 0.0};
    EXPECT_EQ(an::StackModel::check(gesdt), an::StackModelError::None);

    // No rejection reads no record at all.
    an::StackRequest none = baseRequest();
    none.rejection.sigma = {.low = kNaN, .high = -1.0};
    none.rejection.medianFractions = {.low = 0.0, .high = 0.0};
    none.rejection.gesdt = {.maximumOutlierFraction = 2.0, .significance = 2.0};
    EXPECT_EQ(an::StackModel::check(none), an::StackModelError::None);
}

TEST(StackingModelTest, FrameCountsAndTheReferenceAreBounded)
{
    EXPECT_EQ(an::StackModel::check(baseRequest(0u)), an::StackModelError::NoFrames);
    EXPECT_EQ(an::StackModel::check(baseRequest(1u)), an::StackModelError::None);
    EXPECT_EQ(an::StackModel::check(baseRequest(an::kMaxStackFrames)), an::StackModelError::None);
    EXPECT_EQ(an::StackModel::check(baseRequest(an::kMaxStackFrames + 1u)), an::StackModelError::TooManyFrames);
    EXPECT_EQ(an::kMaxStackFrames, an::kMaxPhotometricWeightFrames);

    an::StackRequest two = baseRequest(2u);
    two.rejection.method = an::StackRejection::SigmaClipping;
    EXPECT_EQ(an::StackModel::check(two), an::StackModelError::RejectionNeedsMoreFrames);
    two.rejection.method = an::StackRejection::None;
    EXPECT_EQ(an::StackModel::check(two), an::StackModelError::None);
    an::StackRequest three = baseRequest(an::kMinimumRejectionFrames);
    three.rejection.method = an::StackRejection::GeneralizedEsdTest;
    EXPECT_EQ(an::StackModel::check(three), an::StackModelError::None);

    an::StackRequest reference = baseRequest(5u);
    reference.normalization.referenceFrame = 5u;
    EXPECT_EQ(an::StackModel::check(reference), an::StackModelError::ReferenceFrameOutOfRange);
    reference.normalization.referenceFrame = 4u;
    EXPECT_EQ(an::StackModel::check(reference), an::StackModelError::None);

    // The reference is judged before the rejection, so a request with both problems names it.
    an::StackRequest both = baseRequest(2u);
    both.normalization.referenceFrame = 2u;
    both.rejection.method = an::StackRejection::SigmaClipping;
    EXPECT_EQ(an::StackModel::check(both), an::StackModelError::ReferenceFrameOutOfRange);
}

TEST(StackingModelTest, WeightingParametersAreValidatedPerSource)
{
    for (const an::StackWeighting floored : {an::StackWeighting::StarCount, an::StackWeighting::Fwhm})
    {
        an::StackRequest request = baseRequest();
        request.weighting.method = floored;
        for (const double bad : {-0.1, 1.5, kNaN, kInfinity})
        {
            request.weighting.minimumWeight = bad;
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::MinimumWeightOutOfRange)
                << an::stackWeightingName(floored) << ' ' << bad;
        }
        for (const double good : {0.0, 0.01, 1.0})
        {
            request.weighting.minimumWeight = good;
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
                << an::stackWeightingName(floored) << ' ' << good;
        }
        EXPECT_TRUE(an::stackWeightingUsesMinimumWeight(floored));
    }
    for (const an::StackWeighting unfloored :
         {an::StackWeighting::Uniform, an::StackWeighting::Explicit, an::StackWeighting::Noise,
          an::StackWeighting::ImageCount, an::StackWeighting::Photometric, an::StackWeighting::Exposure,
          an::StackWeighting::SignalToNoise})
    {
        an::StackRequest request = baseRequest();
        request.weighting.method = unfloored;
        request.weighting.minimumWeight = 7.0;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None) << an::stackWeightingName(unfloored);
        EXPECT_FALSE(an::stackWeightingUsesMinimumWeight(unfloored));
    }

    an::StackRequest exponential = baseRequest();
    exponential.weighting.method = an::StackWeighting::StarCount;
    exponential.weighting.starCount.scaling = an::StarCountScaling::Exponential;
    for (const double bad : {0.0, -2.0, kNaN, kInfinity})
    {
        exponential.weighting.starCount.exponentialAlpha = bad;
        EXPECT_EQ(an::StackModel::check(exponential), an::StackModelError::ExponentialAlphaNotPositive) << bad;
    }
    exponential.weighting.starCount.exponentialAlpha = 0.5;
    EXPECT_EQ(an::StackModel::check(exponential), an::StackModelError::None);
    exponential.weighting.starCount.scaling = an::StarCountScaling::Quadratic;
    exponential.weighting.starCount.exponentialAlpha = 0.0;
    EXPECT_EQ(an::StackModel::check(exponential), an::StackModelError::None);

    an::StackRequest photometric = baseRequest();
    photometric.weighting.method = an::StackWeighting::Photometric;
    for (const double bad : {-1.0, kNaN, kInfinity})
    {
        photometric.weighting.photometric = an::PhotometricWeightingRequest{.uncertaintyPenaltyPerMagnitude = bad};
        EXPECT_EQ(an::StackModel::check(photometric), an::StackModelError::PhotometricPenaltyOutOfRange) << bad;
        photometric.weighting.photometric = an::PhotometricWeightingRequest{.alignmentPenalty = bad};
        EXPECT_EQ(an::StackModel::check(photometric), an::StackModelError::PhotometricPenaltyOutOfRange) << bad;
    }
    for (const double bad : {-0.5, 2.0, kNaN})
    {
        photometric.weighting.photometric = an::PhotometricWeightingRequest{.minimumRawWeight = bad};
        EXPECT_EQ(an::StackModel::check(photometric), an::StackModelError::PhotometricMinimumWeightOutOfRange) << bad;
    }
    photometric.weighting.photometric = an::PhotometricWeightingRequest{};
    EXPECT_EQ(an::StackModel::check(photometric), an::StackModelError::None);
    photometric.weighting.method = an::StackWeighting::Noise;
    photometric.weighting.photometric = an::PhotometricWeightingRequest{.uncertaintyPenaltyPerMagnitude = kNaN};
    EXPECT_EQ(an::StackModel::check(photometric), an::StackModelError::None);
}

TEST(StackingModelTest, LocalWeightingIsValidatedWhenEnabled)
{
    an::StackRequest disabled = baseRequest();
    disabled.localWeighting = {.enabled = false, .windowSize = 0u, .gridSpacing = 0u, .snrExponent = -1.0};
    EXPECT_EQ(an::StackModel::check(disabled), an::StackModelError::None);
    EXPECT_FALSE(an::StackModel::create(disabled)->usesLocalWeights());

    an::StackRequest request = baseRequest();
    request.localWeighting.enabled = true;
    for (const std::uint32_t narrow : {0u, 1u, 2u})
    {
        request.localWeighting.windowSize = narrow;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::LocalWeightWindowTooSmall) << narrow;
    }
    for (const std::uint32_t even : {4u, 6u, 64u})
    {
        request.localWeighting.windowSize = even;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::LocalWeightWindowNotOdd) << even;
    }
    request.localWeighting.windowSize = an::kMinimumLocalWeightWindow;
    for (const std::uint32_t fine : {0u, 1u, 3u})
    {
        request.localWeighting.gridSpacing = fine;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::LocalWeightGridSpacingTooSmall) << fine;
    }
    request.localWeighting.gridSpacing = an::kMinimumLocalWeightGridSpacing;
    for (const double bad : {0.0, -1.0, kNaN, kInfinity})
    {
        request.localWeighting.snrExponent = bad;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::LocalWeightSnrExponentNotPositive) << bad;
    }
    request.localWeighting = {.enabled = true};
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None);
    EXPECT_TRUE(an::StackModel::create(request)->usesLocalWeights());
    EXPECT_EQ(request.localWeighting.windowSize, an::kDefaultLocalWeightWindow);
    EXPECT_EQ(request.localWeighting.gridSpacing, an::kDefaultLocalWeightGridSpacing);
    EXPECT_EQ(request.localWeighting.snrExponent, an::kDefaultLocalWeightSnrExponent);
}

TEST(StackingModelTest, TheNormalizationScopeAndMethodAgree)
{
    for (const an::StackNormalizationScope global :
         {an::StackNormalizationScope::Global, an::StackNormalizationScope::LocalThenGlobal})
    {
        an::StackRequest request = baseRequest();
        request.normalization.scope = global;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::GlobalNormalizationMethodMissing)
            << an::stackNormalizationScopeName(global);
        for (std::uint8_t value = 0u; an::isNormalizationMethod(static_cast<an::NormalizationMethod>(value)); ++value)
        {
            request.normalization.globalMethod = static_cast<an::NormalizationMethod>(value);
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
                << an::normalizationMethodName(*request.normalization.globalMethod);
        }
        for (std::uint8_t value = 0u; an::isScaleEstimator(static_cast<an::ScaleEstimator>(value)); ++value)
        {
            request.normalization.scaleEstimator = static_cast<an::ScaleEstimator>(value);
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
                << an::scaleEstimatorName(request.normalization.scaleEstimator);
        }
        EXPECT_TRUE(an::stackNormalizationScopeUsesGlobalMethod(global));
    }
    for (const an::StackNormalizationScope local :
         {an::StackNormalizationScope::None, an::StackNormalizationScope::Local})
    {
        an::StackRequest request = baseRequest();
        request.normalization.scope = local;
        request.normalization.globalMethod = an::NormalizationMethod::Additive;
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::GlobalNormalizationMethodUnused)
            << an::stackNormalizationScopeName(local);
        request.normalization.globalMethod.reset();
        EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None);
        EXPECT_FALSE(an::stackNormalizationScopeUsesGlobalMethod(local));
    }

    EXPECT_FALSE(an::normalizationMethodMatchesDispersion(an::NormalizationMethod::Additive));
    EXPECT_FALSE(an::normalizationMethodMatchesDispersion(an::NormalizationMethod::Multiplicative));
    EXPECT_TRUE(an::normalizationMethodMatchesDispersion(an::NormalizationMethod::AdditiveScaling));
    EXPECT_TRUE(an::normalizationMethodMatchesDispersion(an::NormalizationMethod::MultiplicativeScaling));
    EXPECT_EQ(an::StackNormalizationRequest{}.scaleEstimator, an::ScaleEstimator::IterativeKSigma);
}

TEST(StackingModelTest, TheOutputIsFloatingAndDescribedFromTheInput)
{
    for (std::uint8_t value = 0u; an::isSampleType(static_cast<an::SampleType>(value)); ++value)
    {
        const auto input = static_cast<an::SampleType>(value);
        const an::ImageDescriptor descriptor = makeDescriptor(input, an::ChannelSchema::Rgb);
        for (const an::SampleType output : {an::SampleType::Float32, an::SampleType::Float64})
        {
            an::StackRequest request = baseRequest(5u, descriptor);
            request.output.sampleType = output;
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None)
                << an::sampleTypeName(input) << " to " << an::sampleTypeName(output);
            const std::optional<an::StackModel> model = an::StackModel::create(request);
            ASSERT_TRUE(model.has_value());
            const an::ImageDescriptor produced = model->outputDescriptor();
            EXPECT_EQ(produced.sampleType(), output);
            EXPECT_EQ(produced.extent(), descriptor.extent());
            EXPECT_EQ(produced.channelSchema(), descriptor.channelSchema());
            EXPECT_EQ(produced.colorSpace(), descriptor.colorSpace());
            EXPECT_EQ(produced.numericDomain(), descriptor.numericDomain());
            EXPECT_EQ(produced.cfa(), descriptor.cfa());
            EXPECT_EQ(produced.coordinateOrigin(), descriptor.coordinateOrigin());
            EXPECT_EQ(produced.pixelAspectRatio(), descriptor.pixelAspectRatio());
        }
        if (!an::isFloatingSampleType(input))
        {
            an::StackRequest request = baseRequest(5u, descriptor);
            request.output.sampleType = input;
            EXPECT_EQ(an::StackModel::check(request), an::StackModelError::OutputSampleTypeNotFloating)
                << an::sampleTypeName(input);
        }
    }

    an::StackRequest unknown = baseRequest();
    unknown.output.sampleType = static_cast<an::SampleType>(200);
    EXPECT_EQ(an::StackModel::check(unknown), an::StackModelError::UnknownOutputSampleType);

    // A mosaiced raw frame stacks as a mosaiced raw frame: the master keeps its filter array and
    // its domain and gains only a floating representation.
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(32u, 32u, 1u, an::defaultEngineLimits());
    ASSERT_TRUE(extent.has_value());
    const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(cfa.has_value());
    const std::optional<an::ImageDescriptor> mosaic =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::RawSensorCode,
                                                               .cfa = *cfa,
                                                               .coordinateOrigin = an::CoordinateOrigin::BottomLeft,
                                                               .pixelAspectRatio = 1.0});
    ASSERT_TRUE(mosaic.has_value());
    const std::optional<an::StackModel> master = an::StackModel::create(baseRequest(20u, *mosaic));
    ASSERT_TRUE(master.has_value());
    EXPECT_TRUE(master->outputDescriptor().isMosaiced());
    EXPECT_EQ(master->outputDescriptor().numericDomain(), an::NumericDomain::RawSensorCode);
    EXPECT_EQ(master->outputDescriptor().sampleType(), an::SampleType::Float32);
    EXPECT_EQ(master->outputDescriptor().coordinateOrigin(), an::CoordinateOrigin::BottomLeft);
}

TEST(StackingModelTest, TheAccumulationIsJustifiedForEveryRepresentation)
{
    for (std::uint8_t value = 0u; an::isSampleType(static_cast<an::SampleType>(value)); ++value)
    {
        const auto input = static_cast<an::SampleType>(value);
        const std::optional<an::StackModel> model = an::StackModel::create(baseRequest(5u, makeDescriptor(input)));
        ASSERT_TRUE(model.has_value());
        EXPECT_EQ(model->accumulationPrecision(), an::ComputePrecision::Float64);
        EXPECT_EQ(model->accumulationJustification(), an::Float64Justification::StackingAccumulation);
        EXPECT_EQ(model->outputContract(), an::OutputPrecisionContract::NormalizedOrLossy);
        EXPECT_EQ(an::checkComputePrecision(input, model->outputContract(), model->accumulationPrecision(),
                                            model->accumulationJustification()),
                  an::PrecisionError::None)
            << an::sampleTypeName(input);
    }
}

TEST(StackingModelTest, TheDeclaredEffectIsConsistent)
{
    EXPECT_EQ(an::checkOperationEffect(an::kStackOperationEffect), an::OperationEffectError::None);
    EXPECT_EQ(an::kStackOperationEffect.samples, an::SampleEffect::Writes);
    EXPECT_EQ(an::kStackOperationEffect.description, an::DescriptionEffect::Restates);
    EXPECT_EQ(an::kStackOperationEffect.semantics, an::SemanticEffect::Restates);
    EXPECT_EQ(an::kStackOperationEffect.solution, an::SolutionEffect::Preserves);
    EXPECT_EQ(an::kStackOperationEffect.codecMetadata, an::CodecMetadataEffect::Drops);
    EXPECT_EQ(an::kStackOperationEffect.outputIntent, an::OutputIntentEffect::Withdraws);
    EXPECT_EQ(an::kStackOperationEffect.validity.policy, an::ValidityPolicy::Regenerate);
    const std::optional<an::ValidityDescriptor> mask =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    ASSERT_TRUE(mask.has_value());
    EXPECT_EQ(an::kStackOperationEffect.validity.produced, *mask);

    // Regeneration needs no input claim to answer, which is what lets the model declare it before
    // any frame exists.
    const an::Result<an::ValidityDescriptor> produced = an::operationValidity(an::kStackOperationEffect, {});
    ASSERT_TRUE(produced.hasValue()) << produced.status().message();
    EXPECT_EQ(*produced, *mask);
}

TEST(StackingModelTest, MakeStackModelReportsTypedFailures)
{
    const an::StackRequest accepted = baseRequest();
    const an::Result<an::StackModel> model = an::makeStackModel(accepted);
    ASSERT_TRUE(model.hasValue()) << model.status().message();
    EXPECT_EQ(*model, *an::StackModel::create(accepted));

    const an::Result<an::StackModel> empty = an::makeStackModel(baseRequest(0u));
    ASSERT_FALSE(empty.hasValue());
    EXPECT_EQ(empty.status().code(), an::ErrorCode::EmptySelection);
    EXPECT_TRUE(messageNames(empty.status(), "NoFrames")) << empty.status().message();
    ASSERT_NE(empty.status().detail(), nullptr);
    EXPECT_EQ(empty.status().detail()->operation, "an::makeStackModel");
    EXPECT_EQ(empty.status().detail()->domain, an::ErrorDomain::Algorithms);

    const an::Result<an::StackModel> tooMany = an::makeStackModel(baseRequest(an::kMaxStackFrames + 3u));
    ASSERT_FALSE(tooMany.hasValue());
    EXPECT_EQ(tooMany.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_TRUE(messageNames(tooMany.status(), "TooManyFrames"));
    EXPECT_EQ(countField(tooMany.status(), an::ContextKey::Requested), an::kMaxStackFrames + 3u);
    EXPECT_EQ(countField(tooMany.status(), an::ContextKey::Limit), an::kMaxStackFrames);

    an::StackRequest reference = baseRequest(4u);
    reference.normalization.referenceFrame = 9u;
    const an::Result<an::StackModel> outOfRange = an::makeStackModel(reference);
    ASSERT_FALSE(outOfRange.hasValue());
    EXPECT_EQ(outOfRange.status().code(), an::ErrorCode::OutOfRange);
    EXPECT_TRUE(messageNames(outOfRange.status(), "ReferenceFrameOutOfRange"));
    EXPECT_EQ(countField(outOfRange.status(), an::ContextKey::Requested), 9u);
    EXPECT_EQ(countField(outOfRange.status(), an::ContextKey::Limit), 4u);
    EXPECT_EQ(textField(outOfRange.status(), an::ContextKey::Parameter), "normalization.referenceFrame");

    an::StackRequest fewFrames = baseRequest(2u);
    fewFrames.rejection.method = an::StackRejection::LinearFitClipping;
    const an::Result<an::StackModel> tooFew = an::makeStackModel(fewFrames);
    ASSERT_FALSE(tooFew.hasValue());
    EXPECT_EQ(tooFew.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(tooFew.status(), "RejectionNeedsMoreFrames"));
    EXPECT_EQ(countField(tooFew.status(), an::ContextKey::Requested), 2u);
    EXPECT_EQ(countField(tooFew.status(), an::ContextKey::Limit), an::kMinimumRejectionFrames);

    an::StackRequest threshold = baseRequest();
    threshold.rejection.method = an::StackRejection::SigmaClipping;
    threshold.rejection.sigma.low = 0.0;
    const an::Result<an::StackModel> sigma = an::makeStackModel(threshold);
    ASSERT_FALSE(sigma.hasValue());
    EXPECT_EQ(sigma.status().code(), an::ErrorCode::OutOfRange);
    EXPECT_TRUE(messageNames(sigma.status(), "SigmaThresholdNotPositive"));
    EXPECT_EQ(textField(sigma.status(), an::ContextKey::Parameter), "rejection.sigma");

    an::StackRequest weighted = baseRequest();
    weighted.combination = an::StackCombination::Median;
    weighted.weighting.method = an::StackWeighting::Explicit;
    const an::Result<an::StackModel> median = an::makeStackModel(weighted);
    ASSERT_FALSE(median.hasValue());
    EXPECT_EQ(median.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_TRUE(messageNames(median.status(), "WeightingNotAdmitted"));
    EXPECT_EQ(textField(median.status(), an::ContextKey::Parameter), "weighting.method");

    an::StackRequest unknown = baseRequest();
    unknown.combination = static_cast<an::StackCombination>(77);
    const an::Result<an::StackModel> refused = an::makeStackModel(unknown);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(messageNames(refused.status(), "UnknownCombination"));

    an::StackRequest integer = baseRequest();
    integer.output.sampleType = an::SampleType::UInt16;
    const an::Result<an::StackModel> notFloating = an::makeStackModel(integer);
    ASSERT_FALSE(notFloating.hasValue());
    EXPECT_EQ(notFloating.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(notFloating.status(), an::ContextKey::Parameter), "output.sampleType");

    // Every reason but `None` maps to a real code, and a value outside the enumeration is a
    // defect rather than a kind of refusal.
    for (std::size_t value = 1u; value < an::kStackModelErrorCount; ++value)
    {
        const an::ErrorCode code = an::stackModelErrorCode(static_cast<an::StackModelError>(value));
        EXPECT_NE(code, an::ErrorCode::Ok) << value;
        EXPECT_NE(code, an::ErrorCode::Internal) << value;
    }
    EXPECT_EQ(an::stackModelErrorCode(static_cast<an::StackModelError>(250)), an::ErrorCode::Internal);
}

TEST(StackingModelTest, ExplicitWeightsAreCheckedAgainstTheModel)
{
    const std::optional<an::StackModel> uniform = an::StackModel::create(baseRequest());
    ASSERT_TRUE(uniform.has_value());
    const std::array<double, 5u> five{1.0, 2.0, 3.0, 4.0, 5.0};
    EXPECT_TRUE(an::checkStackFrameWeights(*uniform, an::StackFrameWeights{}).ok());
    const an::Status unexpected = an::checkStackFrameWeights(*uniform, an::StackFrameWeights{.perFrame = five});
    EXPECT_EQ(unexpected.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(unexpected, an::ContextKey::Parameter), "weighting.method");

    an::StackRequest request = baseRequest(5u, makeDescriptor(an::SampleType::Float32, an::ChannelSchema::Rgb));
    request.weighting.method = an::StackWeighting::Explicit;
    const std::optional<an::StackModel> model = an::StackModel::create(request);
    ASSERT_TRUE(model.has_value());

    EXPECT_EQ(an::checkStackFrameWeights(*model, an::StackFrameWeights{}).code(), an::ErrorCode::IncompatibleArguments);
    const std::array<double, 15u> fifteen{1.0, 1.0, 1.0, 2.0, 2.0, 2.0, 3.0, 3.0, 3.0, 4.0, 4.0, 4.0, 5.0, 5.0, 5.0};
    EXPECT_EQ(
        an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = five, .perFrameChannel = fifteen}).code(),
        an::ErrorCode::IncompatibleArguments);

    const an::Status shortSpan =
        an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = std::span<const double>(five).first(4u)});
    EXPECT_EQ(shortSpan.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(shortSpan, an::ContextKey::Parameter), "perFrame");
    EXPECT_EQ(countField(shortSpan, an::ContextKey::Requested), 4u);
    EXPECT_EQ(countField(shortSpan, an::ContextKey::Total), 5u);
    EXPECT_TRUE(an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = five}).ok());

    const std::array<double, 5u> notFinite{1.0, 1.0, kNaN, 1.0, 1.0};
    const an::Status nan = an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = notFinite});
    EXPECT_EQ(nan.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(nan, an::ContextKey::Parameter), "perFrame[2]");
    const std::array<double, 5u> negative{1.0, 1.0, 1.0, 1.0, -0.5};
    EXPECT_EQ(an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = negative}).code(),
              an::ErrorCode::InvalidArgument);
    const std::array<double, 5u> zeros{0.0, 0.0, 0.0, 0.0, 0.0};
    EXPECT_EQ(an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = zeros}).code(),
              an::ErrorCode::EmptySelection);
    const std::array<double, 5u> oneFrame{0.0, 0.0, 2.5, 0.0, 0.0};
    EXPECT_TRUE(an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrame = oneFrame}).ok());

    const an::Status shortChannels = an::checkStackFrameWeights(
        *model, an::StackFrameWeights{.perFrameChannel = std::span<const double>(fifteen).first(14u)});
    EXPECT_EQ(shortChannels.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(textField(shortChannels, an::ContextKey::Parameter), "perFrameChannel");
    EXPECT_EQ(countField(shortChannels, an::ContextKey::Total), 15u);
    EXPECT_TRUE(an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrameChannel = fifteen}).ok());

    // Channel one carries no weight in any frame while its neighbours do.
    const std::array<double, 15u> deadChannel{1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0,
                                              1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0};
    const an::Status dead = an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrameChannel = deadChannel});
    EXPECT_EQ(dead.code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(countField(dead, an::ContextKey::Requested), 1u);
    const std::array<double, 15u> badChannel{1.0, 1.0, 1.0, 1.0, kInfinity, 1.0, 1.0, 1.0,
                                             1.0, 1.0, 1.0, 1.0, 1.0,       1.0, 1.0};
    const an::Status infinite =
        an::checkStackFrameWeights(*model, an::StackFrameWeights{.perFrameChannel = badChannel});
    EXPECT_EQ(infinite.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(textField(infinite, an::ContextKey::Parameter), "perFrameChannel[4]");
}

TEST(StackingModelTest, NamesAreStableAndTheVocabulariesAreClosed)
{
    EXPECT_EQ(an::kStackModelErrorCount, 35u);
    std::vector<std::string> names;
    for (std::size_t value = 0u; value < an::kStackModelErrorCount; ++value)
    {
        const auto error = static_cast<an::StackModelError>(value);
        EXPECT_TRUE(an::isStackModelError(error));
        const std::string name = an::stackModelErrorName(error);
        EXPECT_NE(name, "UnknownStackModelError") << value;
        for (const std::string& earlier : names)
        {
            EXPECT_NE(earlier, name);
        }
        names.push_back(name);
    }
    EXPECT_FALSE(an::isStackModelError(static_cast<an::StackModelError>(an::kStackModelErrorCount)));
    EXPECT_STREQ(an::stackModelErrorName(static_cast<an::StackModelError>(250)), "UnknownStackModelError");
    EXPECT_STREQ(an::stackModelErrorName(an::StackModelError::None), "None");
    EXPECT_STREQ(an::stackModelErrorName(an::StackModelError::RejectionMapsWithoutRejection),
                 "RejectionMapsWithoutRejection");

    EXPECT_STREQ(an::stackRejectionName(an::StackRejection::MedianSigmaReplacement), "MedianSigmaReplacement");
    EXPECT_STREQ(an::stackRejectionName(static_cast<an::StackRejection>(250)), "UnknownStackRejection");
    EXPECT_FALSE(an::isStackRejection(static_cast<an::StackRejection>(8)));
    EXPECT_STREQ(an::stackWeightingName(an::StackWeighting::Photometric), "Photometric");
    EXPECT_STREQ(an::stackWeightingName(an::StackWeighting::Exposure), "Exposure");
    EXPECT_STREQ(an::stackWeightingName(an::StackWeighting::SignalToNoise), "SignalToNoise");
    EXPECT_STREQ(an::stackWeightingName(static_cast<an::StackWeighting>(250)), "UnknownStackWeighting");
    static_assert(an::isStackWeighting(an::StackWeighting::SignalToNoise));
    EXPECT_FALSE(an::isStackWeighting(static_cast<an::StackWeighting>(9)));
    EXPECT_STREQ(an::starCountScalingName(an::StarCountScaling::RoundnessWeighted), "RoundnessWeighted");
    EXPECT_STREQ(an::starCountScalingName(static_cast<an::StarCountScaling>(250)), "UnknownStarCountScaling");
    EXPECT_FALSE(an::isStarCountScaling(static_cast<an::StarCountScaling>(4)));
    EXPECT_STREQ(an::stackNormalizationScopeName(an::StackNormalizationScope::LocalThenGlobal), "LocalThenGlobal");
    EXPECT_STREQ(an::stackNormalizationScopeName(static_cast<an::StackNormalizationScope>(250)),
                 "UnknownStackNormalizationScope");
    EXPECT_FALSE(an::isStackNormalizationScope(static_cast<an::StackNormalizationScope>(4)));
    EXPECT_STREQ(an::rejectionMapFormName(an::RejectionMapForm::LowAndHigh), "LowAndHigh");
    EXPECT_STREQ(an::rejectionMapFormName(static_cast<an::RejectionMapForm>(250)), "UnknownRejectionMapForm");
    EXPECT_FALSE(an::isRejectionMapForm(static_cast<an::RejectionMapForm>(3)));
    EXPECT_STREQ(an::normalizationMethodName(an::NormalizationMethod::MultiplicativeScaling), "MultiplicativeScaling");
    EXPECT_STREQ(an::scaleEstimatorName(an::ScaleEstimator::IterativeKSigma), "IterativeKSigma");
}

TEST(StackingModelTest, UnknownEnumeratorsAreRefusedBeforeAnyRule)
{
    // Membership is judged before every other rule, in declaration order, so a request that is
    // wrong in several ways names the first unknown value rather than a rule about it.
    an::StackRequest request = baseRequest(0u);
    request.combination = static_cast<an::StackCombination>(9);
    request.rejection.method = static_cast<an::StackRejection>(9);
    request.weighting.method = static_cast<an::StackWeighting>(9);
    request.weighting.starCount.scaling = static_cast<an::StarCountScaling>(9);
    request.normalization.scope = static_cast<an::StackNormalizationScope>(9);
    request.normalization.globalMethod = static_cast<an::NormalizationMethod>(9);
    request.normalization.scaleEstimator = static_cast<an::ScaleEstimator>(9);
    request.output.rejectionMaps = static_cast<an::RejectionMapForm>(9);
    request.output.sampleType = static_cast<an::SampleType>(99);

    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownCombination);
    request.combination = an::StackCombination::Average;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownRejection);
    request.rejection.method = an::StackRejection::None;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownWeighting);
    request.weighting.method = an::StackWeighting::Uniform;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownStarCountScaling);
    request.weighting.starCount.scaling = an::StarCountScaling::Quadratic;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownNormalizationScope);
    request.normalization.scope = an::StackNormalizationScope::Global;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownNormalizationMethod);
    request.normalization.globalMethod = an::NormalizationMethod::Additive;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownScaleEstimator);
    request.normalization.scaleEstimator = an::ScaleEstimator::MedianAbsoluteDeviation;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownRejectionMapForm);
    request.output.rejectionMaps = an::RejectionMapForm::None;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::UnknownOutputSampleType);
    request.output.sampleType = an::SampleType::Float32;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::NoFrames);
    request.input.frameCount = 3u;
    EXPECT_EQ(an::StackModel::check(request), an::StackModelError::None);
}

TEST(StackingModelTest, RequestsCompareByValue)
{
    const an::StackRequest left = baseRequest();
    an::StackRequest right = baseRequest();
    EXPECT_EQ(left, right);
    EXPECT_EQ(*an::StackModel::create(left), *an::StackModel::create(right));

    // An inert record still participates in equality, so two requests that check identically can
    // still be told apart.
    right.rejection.medianFractions.low = 0.5;
    EXPECT_NE(left, right);
    EXPECT_EQ(an::StackModel::check(left), an::StackModelError::None);
    EXPECT_EQ(an::StackModel::check(right), an::StackModelError::None);
    EXPECT_NE(*an::StackModel::create(left), *an::StackModel::create(right));

    an::StackRequest other = baseRequest(6u);
    EXPECT_NE(left, other);
    other.input.frameCount = 5u;
    EXPECT_EQ(left, other);
    other.normalization.globalMethod = an::NormalizationMethod::Additive;
    EXPECT_NE(left, other);
}

TEST(StackingModelTest, TheModelIsAConstantExpression)
{
    ASSERT_TRUE(kConstantModel.has_value());
    EXPECT_EQ(kConstantModel->request(), kConstantRequest);
    EXPECT_EQ(kConstantModel->outputDescriptor().sampleType(), an::SampleType::Float64);
    EXPECT_EQ(kConstantModel->outputDescriptor().extent().pixelCount(), 256u);
    EXPECT_EQ(an::makeStackModel(kConstantRequest).hasValue(), true);
}

} // namespace
