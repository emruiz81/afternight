// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the spectrophotometric gain fit: the white-reference anchor, the line, the
// clipping policies, the verdicts, low support, subsampling and thread independence.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricFit.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "ColorTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <vector>

namespace
{

using color_test::makeEngine;
using color_test::makeExecution;

[[nodiscard]] an::SpectrophotometricSample acceptedSample(std::uint64_t identifier, double catalogRedToGreen,
                                                          double catalogBlueToGreen, double observedRedScale,
                                                          double observedBlueScale)
{
    an::SpectrophotometricSample sample;
    sample.sourceIdentifier = identifier;
    sample.accepted = true;
    sample.catalogRelativeToGreen = {catalogRedToGreen, 1.0, catalogBlueToGreen};
    sample.imageRelativeToGreen = {catalogRedToGreen * observedRedScale, 1.0, catalogBlueToGreen * observedBlueScale};
    return sample;
}

[[nodiscard]] std::vector<an::SpectrophotometricSample> wellSpanned(std::size_t count, double observedRedScale,
                                                                    double observedBlueScale)
{
    std::vector<an::SpectrophotometricSample> samples;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double offset = static_cast<double>(index);
        samples.push_back(acceptedSample(700000000000ull + index, 0.65 + 0.05 * offset, 1.45 - 0.04 * offset,
                                         observedRedScale, observedBlueScale));
    }
    return samples;
}

[[nodiscard]] std::vector<an::SpectrophotometricSample> positiveRatio(std::size_t count, double observedRedScale,
                                                                      double observedBlueScale)
{
    std::vector<an::SpectrophotometricSample> samples;
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double offset = static_cast<double>(index);
        samples.push_back(acceptedSample(730000000000ull + index, 0.70 + 0.01 * offset, 1.45 - 0.01 * offset,
                                         observedRedScale, observedBlueScale));
    }
    return samples;
}

[[nodiscard]] an::SpectrophotometricFitRequest permissive()
{
    an::SpectrophotometricFitRequest request;
    request.minimumSamples = 5u;
    request.minimumCatalogRatioSpan = 0.1;
    return request;
}

[[nodiscard]] std::array<double, 3u> maxNormalized(const std::array<double, 3u>& raw)
{
    const double largest = (std::max)((std::max)(raw[0], raw[1]), raw[2]);
    return {raw[0] / largest, raw[1] / largest, raw[2] / largest};
}

class RecordingDiagnosticSink final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        ++count;
        severity = event.severity;
    }

    std::uint32_t count = 0u;
    an::DiagnosticSeverity severity = an::DiagnosticSeverity::Trace;
};

TEST(SpectrophotometricFitTest, DefaultsAreTheDocumentedSupportEnvelope)
{
    const an::SpectrophotometricFitRequest request;
    EXPECT_EQ(request.minimumSamples, 35u);
    EXPECT_DOUBLE_EQ(request.minimumCatalogRatioSpan, 0.15);
    EXPECT_DOUBLE_EQ(request.clipSigmas, 3.0);
    EXPECT_EQ(request.maximumIterations, 4u);
    EXPECT_DOUBLE_EQ(request.minimumRobustScale, 1.0e-6);
    EXPECT_DOUBLE_EQ(request.maximumRelativeGainUncertainty, 0.05);
    EXPECT_EQ(request.lowSupportSamples, 60u);
    EXPECT_EQ(request.outlierPolicy, an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit);
    EXPECT_EQ(request.whiteReferenceGains, (std::array<double, 3u>{1.0, 1.0, 1.0}));
    EXPECT_EQ(an::kSpectrophotometricExactFitPointLimit, 1024u);
    EXPECT_STREQ(
        an::spectrophotometricOutlierPolicyName(an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask),
        "SinglePassRepeatedMedianMask");
    EXPECT_FALSE(an::isSpectrophotometricOutlierPolicy(static_cast<an::SpectrophotometricOutlierPolicy>(2u)));
    EXPECT_STREQ(an::spectrophotometricFitVerdictName(an::SpectrophotometricFitVerdict::InsufficientCatalogSpan),
                 "InsufficientCatalogSpan");
    EXPECT_FALSE(an::isSpectrophotometricFitVerdict(static_cast<an::SpectrophotometricFitVerdict>(6u)));
}

TEST(SpectrophotometricFitTest, RecoversKnownGainsWithWhiteReferenceNormalization)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::SpectrophotometricSample> samples = wellSpanned(12u, 1.25, 0.8);
    an::SpectrophotometricFitRequest request = permissive();
    request.whiteReferenceGains = {0.5, 1.0, 2.0};
    const an::Result<an::SpectrophotometricFitResult> fit = an::fitSpectrophotometricGains(execution, samples, request);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    ASSERT_TRUE(fit->accepted()) << an::spectrophotometricFitVerdictName(fit->verdict);
    EXPECT_EQ(fit->suppliedCount, 12u);
    EXPECT_EQ(fit->acceptedCount, 12u);
    EXPECT_EQ(fit->usableCount, 12u);
    EXPECT_TRUE(fit->red.fitted);
    EXPECT_TRUE(fit->blue.fitted);
    EXPECT_NEAR(fit->red.whiteReferenceCatalogRatio, 2.0, 1.0e-12);
    EXPECT_NEAR(fit->blue.whiteReferenceCatalogRatio, 0.5, 1.0e-12);
    EXPECT_NEAR(fit->red.observedToCatalogScale, 1.25, 1.0e-12);
    EXPECT_NEAR(fit->blue.observedToCatalogScale, 0.8, 1.0e-12);
    EXPECT_NEAR(fit->red.rawGain, 0.4, 1.0e-12);
    EXPECT_NEAR(fit->blue.rawGain, 2.5, 1.0e-12);
    const std::array<double, 3u> expected = maxNormalized({0.4, 1.0, 2.5});
    EXPECT_NEAR(fit->gains[0], expected[0], 1.0e-12);
    EXPECT_NEAR(fit->gains[1], expected[1], 1.0e-12);
    EXPECT_NEAR(fit->gains[2], expected[2], 1.0e-12);
    EXPECT_NEAR(fit->red.gain, expected[0], 1.0e-12);
    EXPECT_NEAR(fit->blue.gain, expected[2], 1.0e-12);
    EXPECT_EQ(fit->red.rejectedCount, 0u);
    EXPECT_EQ(fit->blue.rejectedCount, 0u);
    EXPECT_EQ(fit->red.inlierCount, 12u);
    EXPECT_NEAR(fit->red.rmsLogResidual, 0.0, 1.0e-12);
    EXPECT_NEAR(fit->blue.madLogResidual, 0.0, 1.0e-12);
    EXPECT_GT(fit->catalogRedRatioSpan, 0.5);
    EXPECT_EQ(fit->supportCount, 12u);
    EXPECT_TRUE(fit->lowSupport);
    EXPECT_TRUE(fit->rejectedSampleIndices.empty());
}

TEST(SpectrophotometricFitTest, LinearFitIsEvaluatedAtTheWhiteReferenceRatio)
{
    constexpr double kRedIntercept = 0.11;
    constexpr double kRedSlope = 1.32;
    constexpr double kBlueIntercept = 0.07;
    constexpr double kBlueSlope = 0.64;
    constexpr double kRedWhiteRatio = 1.20;
    constexpr double kBlueWhiteRatio = 0.85;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::SpectrophotometricSample> samples;
    for (std::size_t index = 0u; index < 18u; ++index)
    {
        const double offset = static_cast<double>(index);
        an::SpectrophotometricSample sample;
        sample.sourceIdentifier = 760000000000ull + index;
        sample.accepted = true;
        const double catalogRed = 0.62 + 0.055 * offset;
        const double catalogBlue = 1.42 - 0.043 * offset;
        sample.catalogRelativeToGreen = {catalogRed, 1.0, catalogBlue};
        sample.imageRelativeToGreen = {kRedIntercept + kRedSlope * catalogRed, 1.0,
                                       kBlueIntercept + kBlueSlope * catalogBlue};
        samples.push_back(sample);
    }
    an::SpectrophotometricFitRequest request = permissive();
    request.whiteReferenceGains = {1.0 / kRedWhiteRatio, 1.0, 1.0 / kBlueWhiteRatio};
    const an::Result<an::SpectrophotometricFitResult> fit = an::fitSpectrophotometricGains(execution, samples, request);
    ASSERT_TRUE(fit.hasValue()) << fit.status().message();
    ASSERT_TRUE(fit->accepted()) << an::spectrophotometricFitVerdictName(fit->verdict);
    EXPECT_NEAR(fit->red.intercept, kRedIntercept, 1.0e-12);
    EXPECT_NEAR(fit->red.slope, kRedSlope, 1.0e-12);
    EXPECT_NEAR(fit->blue.intercept, kBlueIntercept, 1.0e-12);
    EXPECT_NEAR(fit->blue.slope, kBlueSlope, 1.0e-12);
    EXPECT_NEAR(fit->red.whiteReferenceCatalogRatio, kRedWhiteRatio, 1.0e-12);
    EXPECT_NEAR(fit->blue.whiteReferenceCatalogRatio, kBlueWhiteRatio, 1.0e-12);
    const double rawRed = 1.0 / (kRedIntercept + kRedSlope * kRedWhiteRatio);
    const double rawBlue = 1.0 / (kBlueIntercept + kBlueSlope * kBlueWhiteRatio);
    EXPECT_NEAR(fit->red.rawGain, rawRed, 1.0e-12);
    EXPECT_NEAR(fit->blue.rawGain, rawBlue, 1.0e-12);
    EXPECT_NEAR(fit->red.fittedWhiteReferenceImageRatio, 1.0 / rawRed, 1.0e-12);
    const std::array<double, 3u> expected = maxNormalized({rawRed, 1.0, rawBlue});
    EXPECT_NEAR(fit->gains[0], expected[0], 1.0e-12);
    EXPECT_NEAR(fit->gains[1], expected[1], 1.0e-12);
    EXPECT_NEAR(fit->gains[2], expected[2], 1.0e-12);
    EXPECT_GE(fit->red.gainUncertainty, 0.0);
    EXPECT_LT(fit->red.gainUncertainty / fit->red.gain, request.maximumRelativeGainUncertainty);
}

TEST(SpectrophotometricFitTest, ClipsOutliersUnderBothPolicies)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::SpectrophotometricSample> samples = wellSpanned(10u, 1.2, 0.9);
    samples.push_back(acceptedSample(424242424242ull, 1.3, 0.9, 5.0, 0.15));
    const an::Result<an::SpectrophotometricFitResult> iterative =
        an::fitSpectrophotometricGains(execution, samples, permissive());
    ASSERT_TRUE(iterative.hasValue()) << iterative.status().message();
    ASSERT_TRUE(iterative->accepted()) << an::spectrophotometricFitVerdictName(iterative->verdict);
    EXPECT_NEAR(iterative->red.observedToCatalogScale, 1.2, 1.0e-12);
    EXPECT_NEAR(iterative->blue.observedToCatalogScale, 0.9, 1.0e-12);
    const std::array<double, 3u> expected = maxNormalized({1.0 / 1.2, 1.0, 1.0 / 0.9});
    EXPECT_NEAR(iterative->gains[0], expected[0], 1.0e-12);
    EXPECT_NEAR(iterative->gains[1], expected[1], 1.0e-12);
    EXPECT_NEAR(iterative->gains[2], expected[2], 1.0e-12);
    EXPECT_EQ(iterative->red.rejectedCount, 1u);
    EXPECT_EQ(iterative->blue.rejectedCount, 1u);
    EXPECT_EQ(iterative->red.rejectedSampleIndices, std::vector<std::uint32_t>{10u});
    EXPECT_EQ(iterative->rejectedSampleIndices, std::vector<std::uint32_t>{10u});
    EXPECT_EQ(iterative->red.inlierCount, 10u);

    an::SpectrophotometricFitRequest singlePass = permissive();
    singlePass.outlierPolicy = an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask;
    const an::Result<an::SpectrophotometricFitResult> once =
        an::fitSpectrophotometricGains(execution, samples, singlePass);
    ASSERT_TRUE(once.hasValue()) << once.status().message();
    ASSERT_TRUE(once->accepted()) << an::spectrophotometricFitVerdictName(once->verdict);
    EXPECT_EQ(once->red.rejectedCount, 1u);
    EXPECT_EQ(once->blue.rejectedCount, 1u);
    EXPECT_EQ(once->rejectedSampleIndices, std::vector<std::uint32_t>{10u});
    EXPECT_NEAR(once->red.observedToCatalogScale, 1.2, 1.0e-12);

    std::vector<an::SpectrophotometricSample> fewer = wellSpanned(9u, 1.2, 0.9);
    fewer.push_back(acceptedSample(424242424242ull, 1.3, 0.9, 5.0, 0.15));
    singlePass.minimumSamples = 10u;
    const an::Result<an::SpectrophotometricFitResult> starved =
        an::fitSpectrophotometricGains(execution, fewer, singlePass);
    ASSERT_TRUE(starved.hasValue());
    EXPECT_EQ(starved->verdict, an::SpectrophotometricFitVerdict::InsufficientInliers);
    EXPECT_FALSE(starved->accepted());
    EXPECT_EQ(starved->gains, (std::array<double, 3u>{1.0, 1.0, 1.0}));
}

TEST(SpectrophotometricFitTest, VerdictsNameTheFirstRefusalAndKeepWhatWasComputed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::SpectrophotometricSample> few = wellSpanned(6u, 1.1, 0.95);
    few[1].accepted = false;
    few[2].imageRelativeToGreen[0] = -1.0;
    const an::Result<an::SpectrophotometricFitResult> tooFew =
        an::fitSpectrophotometricGains(execution, few, permissive());
    ASSERT_TRUE(tooFew.hasValue()) << tooFew.status().message();
    EXPECT_EQ(tooFew->verdict, an::SpectrophotometricFitVerdict::InsufficientSamples);
    EXPECT_EQ(tooFew->suppliedCount, 6u);
    EXPECT_EQ(tooFew->acceptedCount, 5u);
    EXPECT_EQ(tooFew->usableCount, 4u);
    EXPECT_FALSE(tooFew->red.fitted);

    std::vector<an::SpectrophotometricSample> flat;
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        flat.push_back(acceptedSample(800000000000ull + index, 1.0, 1.0, 1.05, 0.95));
    }
    const an::Result<an::SpectrophotometricFitResult> narrow =
        an::fitSpectrophotometricGains(execution, flat, permissive());
    ASSERT_TRUE(narrow.hasValue());
    EXPECT_EQ(narrow->verdict, an::SpectrophotometricFitVerdict::InsufficientCatalogSpan);
    EXPECT_EQ(narrow->usableCount, 8u);
    EXPECT_DOUBLE_EQ(narrow->catalogRedRatioSpan, 0.0);

    std::vector<an::SpectrophotometricSample> scattered = positiveRatio(36u, 1.0, 1.0);
    for (std::size_t index = 0u; index < scattered.size(); ++index)
    {
        const double redScatter = (index % 3u) == 0u ? std::exp(0.75) : ((index % 3u) == 1u ? std::exp(-0.75) : 1.0);
        const double blueScatter = (index % 4u) < 2u ? std::exp(0.80) : std::exp(-0.80);
        scattered[index].imageRelativeToGreen[0] *= redScatter;
        scattered[index].imageRelativeToGreen[2] *= blueScatter;
    }
    const an::Result<an::SpectrophotometricFitResult> noisy = an::fitSpectrophotometricGains(execution, scattered);
    ASSERT_TRUE(noisy.hasValue()) << noisy.status().message();
    EXPECT_EQ(noisy->verdict, an::SpectrophotometricFitVerdict::ExcessiveGainUncertainty);
    EXPECT_TRUE(noisy->red.fitted);
    EXPECT_TRUE(noisy->blue.fitted);
    EXPECT_TRUE(std::isfinite(noisy->red.gain));
    EXPECT_GT(noisy->red.gainUncertainty / noisy->red.gain,
              an::kDefaultSpectrophotometricMaximumRelativeGainUncertainty);
    EXPECT_GT(noisy->blue.gainUncertainty / noisy->blue.gain,
              an::kDefaultSpectrophotometricMaximumRelativeGainUncertainty);
    EXPECT_EQ(noisy->gains, (std::array<double, 3u>{1.0, 1.0, 1.0}));
}

TEST(SpectrophotometricFitTest, LowSupportIsReportedAndLargeSetsAreSubsampledIdenticallyOnEveryThreadCount)
{
    // Three executions share this engine at once, so its budget has to hold all three widths.
    const an::EngineContext engine = makeEngine(16u);
    RecordingDiagnosticSink sink;
    an::ExecutionOptions options;
    options.maxConcurrency = 4u;
    options.diagnosticSink = &sink;
    const an::Result<an::ExecutionContext> listened = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(listened.hasValue());
    const an::Result<an::SpectrophotometricFitResult> low =
        an::fitSpectrophotometricGains(*listened, positiveRatio(40u, 1.05, 0.94));
    ASSERT_TRUE(low.hasValue()) << low.status().message();
    ASSERT_TRUE(low->accepted()) << an::spectrophotometricFitVerdictName(low->verdict);
    EXPECT_EQ(low->red.inlierCount, 40u);
    EXPECT_EQ(low->blue.inlierCount, 40u);
    EXPECT_TRUE(low->lowSupport);
    EXPECT_EQ(low->supportCount, 40u);
    EXPECT_EQ(sink.count, 1u);
    EXPECT_EQ(sink.severity, an::DiagnosticSeverity::Warning);
    const an::Result<an::SpectrophotometricFitResult> supported =
        an::fitSpectrophotometricGains(*listened, positiveRatio(60u, 1.05, 0.94));
    ASSERT_TRUE(supported.hasValue());
    EXPECT_FALSE(supported->lowSupport);
    EXPECT_EQ(sink.count, 1u);

    std::vector<an::SpectrophotometricSample> large;
    for (std::size_t index = 0u; index < 10000u; ++index)
    {
        const double red = 0.70 + static_cast<double>(index % 200u) * 0.005;
        const double blue = 0.65 + static_cast<double>((index * 37u) % 200u) * 0.004;
        large.push_back(acceptedSample(700000000000ull + index, red, blue, 1.15, 0.85));
    }
    for (std::size_t index = 0u; index < large.size(); index += 97u)
    {
        large[index].imageRelativeToGreen[0] *= 3.5;
        large[index].imageRelativeToGreen[2] *= 0.25;
    }
    an::SpectrophotometricFitRequest request = permissive();
    request.minimumSamples = 100u;
    request.minimumCatalogRatioSpan = 0.5;
    request.lowSupportSamples = 100u;
    const an::ExecutionContext wide = makeExecution(engine, 6u);
    const an::Result<an::SpectrophotometricFitResult> many = an::fitSpectrophotometricGains(wide, large, request);
    ASSERT_TRUE(many.hasValue()) << many.status().message();
    ASSERT_TRUE(many->accepted()) << an::spectrophotometricFitVerdictName(many->verdict);
    EXPECT_EQ(many->acceptedCount, 10000u);
    EXPECT_GT(many->red.rejectedCount, 0u);
    EXPECT_GT(many->blue.rejectedCount, 0u);
    const std::array<double, 3u> expected = maxNormalized({1.0 / 1.15, 1.0, 1.0 / 0.85});
    EXPECT_NEAR(many->gains[0], expected[0], 1.0e-12);
    EXPECT_NEAR(many->gains[1], expected[1], 1.0e-12);
    EXPECT_NEAR(many->gains[2], expected[2], 1.0e-12);
    EXPECT_FALSE(many->lowSupport);
    EXPECT_TRUE(std::is_sorted(many->rejectedSampleIndices.begin(), many->rejectedSampleIndices.end()));
    EXPECT_EQ(std::adjacent_find(many->rejectedSampleIndices.begin(), many->rejectedSampleIndices.end()),
              many->rejectedSampleIndices.end());

    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::Result<an::SpectrophotometricFitResult> serial = an::fitSpectrophotometricGains(single, large, request);
    ASSERT_TRUE(serial.hasValue());
    EXPECT_EQ(serial->gains, many->gains);
    EXPECT_EQ(serial->red.intercept, many->red.intercept);
    EXPECT_EQ(serial->red.slope, many->red.slope);
    EXPECT_EQ(serial->blue.intercept, many->blue.intercept);
    EXPECT_EQ(serial->blue.slope, many->blue.slope);
    EXPECT_EQ(serial->rejectedSampleIndices, many->rejectedSampleIndices);
    EXPECT_EQ(serial->red.rmsLogResidual, many->red.rmsLogResidual);
}

/// The median an ascending sort gives: the middle value, or the mean of the two middle values.
[[nodiscard]] double sortedMedian(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2u;
    return (values.size() % 2u) == 1u ? values[middle] : (values[middle - 1u] + values[middle]) * 0.5;
}

/// The repeated-median line through every point, worked out here from its definition: each point's median
/// slope to every other point at a different catalog ratio whose slope is finite, the median of those, then
/// the median intercept.
[[nodiscard]] std::array<double, 2u> repeatedMedianOracle(const std::vector<double>& catalog,
                                                          const std::vector<double>& image)
{
    std::vector<double> pointMedians;
    for (std::size_t point = 0u; point < catalog.size(); ++point)
    {
        std::vector<double> slopes;
        for (std::size_t other = 0u; other < catalog.size(); ++other)
        {
            const double dx = catalog[other] - catalog[point];
            if (other == point || dx == 0.0)
            {
                continue;
            }
            const double slope = (image[other] - image[point]) / dx;
            if (std::isfinite(slope))
            {
                slopes.push_back(slope);
            }
        }
        if (!slopes.empty())
        {
            pointMedians.push_back(sortedMedian(slopes));
        }
    }
    const double slope = sortedMedian(pointMedians);
    std::vector<double> intercepts;
    for (std::size_t point = 0u; point < catalog.size(); ++point)
    {
        intercepts.push_back(image[point] - slope * catalog[point]);
    }
    return {sortedMedian(intercepts), slope};
}

// The fitted line is the repeated median to the last bit. Every catalog ratio appears twice, so every point
// has a partner it cannot form a slope with, and the first two samples are the same point; the single-pass
// policy keeps the line fitted through every sample, which the oracle works out from the definition, at one
// worker and at several.
TEST(SpectrophotometricFitTest, TheLineIsTheRepeatedMedianOfEveryPairwiseSlopeExactly)
{
    const an::EngineContext engine = makeEngine(8u);
    std::vector<an::SpectrophotometricSample> samples;
    std::vector<double> red;
    std::vector<double> redImage;
    std::vector<double> blue;
    std::vector<double> blueImage;
    for (std::size_t index = 0u; index < 301u; ++index)
    {
        const std::size_t kept = index == 1u ? 0u : index;
        const double catalogRed = 0.6 + 0.004 * static_cast<double>(kept % 150u);
        const double catalogBlue = 1.5 - 0.003 * static_cast<double>((kept * 7u) % 150u);
        const double redNoise = 0.02 * std::sin(1.7 * static_cast<double>(kept));
        const double blueNoise = 0.03 * std::cos(2.3 * static_cast<double>(kept));
        an::SpectrophotometricSample sample;
        sample.sourceIdentifier = 900000000000ull + index;
        sample.accepted = true;
        sample.catalogRelativeToGreen = {catalogRed, 1.0, catalogBlue};
        sample.imageRelativeToGreen = {0.05 + 1.2 * catalogRed + redNoise, 1.0, 0.9 * catalogBlue + blueNoise};
        samples.push_back(sample);
        red.push_back(catalogRed);
        redImage.push_back(sample.imageRelativeToGreen[0]);
        blue.push_back(catalogBlue);
        blueImage.push_back(sample.imageRelativeToGreen[2]);
    }
    const std::array<double, 2u> redLine = repeatedMedianOracle(red, redImage);
    const std::array<double, 2u> blueLine = repeatedMedianOracle(blue, blueImage);
    an::SpectrophotometricFitRequest request = permissive();
    request.outlierPolicy = an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask;
    for (const std::uint32_t workers : {1u, 3u})
    {
        const an::ExecutionContext execution = makeExecution(engine, workers);
        const an::Result<an::SpectrophotometricFitResult> fit =
            an::fitSpectrophotometricGains(execution, samples, request);
        ASSERT_TRUE(fit.hasValue()) << fit.status().message();
        EXPECT_EQ(fit->usableCount, 301u);
        EXPECT_EQ(fit->red.intercept, redLine[0]) << workers << " workers";
        EXPECT_EQ(fit->red.slope, redLine[1]) << workers << " workers";
        EXPECT_EQ(fit->blue.intercept, blueLine[0]) << workers << " workers";
        EXPECT_EQ(fit->blue.slope, blueLine[1]) << workers << " workers";
    }
}

// Above the exact-fit ceiling the line goes through an evenly spaced subset in catalog-ratio order. The
// samples arrive out of that order, every catalog ratio distinct, so the subset is spread across the whole
// set rather than being its first points; the line through it is still the repeated median to the last bit.
TEST(SpectrophotometricFitTest, ALargeSetsLineIsTheRepeatedMedianOfItsEvenlySpacedSubsetExactly)
{
    constexpr std::size_t kCount = 1500u;
    const an::EngineContext engine = makeEngine(8u);
    std::vector<an::SpectrophotometricSample> samples;
    std::vector<double> red;
    std::vector<double> redImage;
    std::vector<double> blue;
    std::vector<double> blueImage;
    for (std::size_t index = 0u; index < kCount; ++index)
    {
        const double catalogRed = 0.5 + 0.001 * static_cast<double>((index * 7919u) % kCount);
        const double catalogBlue = 1.9 - 0.0008 * static_cast<double>((index * 1301u) % kCount);
        an::SpectrophotometricSample sample;
        sample.sourceIdentifier = 910000000000ull + index;
        sample.accepted = true;
        sample.catalogRelativeToGreen = {catalogRed, 1.0, catalogBlue};
        sample.imageRelativeToGreen = {0.03 + 1.1 * catalogRed + 0.02 * std::sin(0.9 * static_cast<double>(index)), 1.0,
                                       0.95 * catalogBlue + 0.025 * std::cos(1.3 * static_cast<double>(index))};
        samples.push_back(sample);
        red.push_back(catalogRed);
        redImage.push_back(sample.imageRelativeToGreen[0]);
        blue.push_back(catalogBlue);
        blueImage.push_back(sample.imageRelativeToGreen[2]);
    }
    const auto subset = [](const std::vector<double>& catalog, const std::vector<double>& image) {
        std::vector<std::size_t> order(catalog.size());
        for (std::size_t index = 0u; index < order.size(); ++index)
        {
            order[index] = index;
        }
        std::sort(order.begin(), order.end(),
                  [&catalog](std::size_t left, std::size_t right) { return catalog[left] < catalog[right]; });
        const std::size_t limit = an::kSpectrophotometricExactFitPointLimit;
        const double step = static_cast<double>(order.size() - 1u) / static_cast<double>(limit - 1u);
        std::vector<double> pickedCatalog;
        std::vector<double> pickedImage;
        std::size_t previous = order.size();
        for (std::size_t sample = 0u; sample < limit; ++sample)
        {
            const auto rank = static_cast<std::size_t>(std::llround(static_cast<double>(sample) * step));
            if (rank != previous)
            {
                pickedCatalog.push_back(catalog[order[(std::min)(rank, order.size() - 1u)]]);
                pickedImage.push_back(image[order[(std::min)(rank, order.size() - 1u)]]);
                previous = rank;
            }
        }
        return repeatedMedianOracle(pickedCatalog, pickedImage);
    };
    const std::array<double, 2u> redLine = subset(red, redImage);
    const std::array<double, 2u> blueLine = subset(blue, blueImage);
    an::SpectrophotometricFitRequest request = permissive();
    request.outlierPolicy = an::SpectrophotometricOutlierPolicy::SinglePassRepeatedMedianMask;
    for (const std::uint32_t workers : {1u, 3u})
    {
        const an::ExecutionContext execution = makeExecution(engine, workers);
        const an::Result<an::SpectrophotometricFitResult> fit =
            an::fitSpectrophotometricGains(execution, samples, request);
        ASSERT_TRUE(fit.hasValue()) << fit.status().message();
        EXPECT_EQ(fit->usableCount, kCount);
        EXPECT_EQ(fit->red.intercept, redLine[0]) << workers << " workers";
        EXPECT_EQ(fit->red.slope, redLine[1]) << workers << " workers";
        EXPECT_EQ(fit->blue.intercept, blueLine[0]) << workers << " workers";
        EXPECT_EQ(fit->blue.slope, blueLine[1]) << workers << " workers";
    }
}

TEST(SpectrophotometricFitTest, RefusesMalformedRequestsAndStops)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::SpectrophotometricSample> samples = wellSpanned(12u, 1.1, 0.9);
    const auto refuses = [&](const an::SpectrophotometricFitRequest& request) {
        return an::fitSpectrophotometricGains(execution, samples, request).status().code();
    };
    an::SpectrophotometricFitRequest request = permissive();
    request.minimumSamples = 0u;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.minimumCatalogRatioSpan = -0.1;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.clipSigmas = 0.0;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.maximumIterations = 0u;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.minimumRobustScale = 0.0;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.maximumRelativeGainUncertainty = 0.0;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.lowSupportSamples = 4u;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.whiteReferenceGains[2] = 0.0;
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    request = permissive();
    request.outlierPolicy = static_cast<an::SpectrophotometricOutlierPolicy>(2u);
    EXPECT_EQ(refuses(request), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::fitSpectrophotometricGains(an::ExecutionContext{}, samples, permissive()).status().code(),
              an::ErrorCode::NotInitialized);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(an::fitSpectrophotometricGains(makeExecution(engine, 2u, stop.get_token()), samples, permissive())
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    const an::Result<an::SpectrophotometricFitResult> empty =
        an::fitSpectrophotometricGains(execution, std::span<const an::SpectrophotometricSample>{}, permissive());
    ASSERT_TRUE(empty.hasValue());
    EXPECT_EQ(empty->verdict, an::SpectrophotometricFitVerdict::InsufficientSamples);
    EXPECT_EQ(empty->suppliedCount, 0u);
}

} // namespace
