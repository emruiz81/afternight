// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for deterministic, uncertainty-aware photometric stack weights.

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine(std::uint32_t concurrency = 4u,
                                           std::uint64_t memoryBudget = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = concurrency;
    options.limits.memoryBudgetBytes = memoryBudget;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 4u,
                                                 std::stop_token stopToken = {},
                                                 an::DiagnosticSink* diagnostics = nullptr)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.stopToken = std::move(stopToken);
    options.diagnosticSink = diagnostics;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// The finite-input calculation, written out here as an independent oracle.
[[nodiscard]] std::vector<double> oracleWeights(const std::vector<an::PhotometricFrameMeasurement>& measurements,
                                                const an::PhotometricWeightingRequest& request = {})
{
    std::vector<std::size_t> measured;
    std::vector<double> rms;
    double minimumZeroPoint = (std::numeric_limits<double>::infinity)();
    for (std::size_t index = 0u; index < measurements.size(); ++index)
    {
        if (!measurements[index].zeroPointMagnitudes.has_value())
        {
            continue;
        }
        measured.push_back(index);
        minimumZeroPoint = (std::min)(minimumZeroPoint, *measurements[index].zeroPointMagnitudes);
        if (measurements[index].astrometricRootMeanSquarePixels.has_value())
        {
            rms.push_back(*measurements[index].astrometricRootMeanSquarePixels);
        }
    }
    if (measured.empty())
    {
        return std::vector<double>(measurements.size(), 1.0);
    }
    std::sort(rms.begin(), rms.end());
    double medianRms = 0.0;
    if (!rms.empty())
    {
        medianRms =
            rms.size() % 2u != 0u ? rms[rms.size() / 2u] : (rms[rms.size() / 2u - 1u] + rms[rms.size() / 2u]) * 0.5;
    }

    std::vector<double> raw(measurements.size(), 0.0);
    std::vector<double> measuredRaw;
    for (std::size_t index : measured)
    {
        const an::PhotometricFrameMeasurement& frame = measurements[index];
        const double transparency = std::pow(10.0, 0.4 * (*frame.zeroPointMagnitudes - minimumZeroPoint));
        const double confidence =
            frame.zeroPointStandardUncertaintyMagnitudes.has_value()
                ? 1.0 / (1.0 + request.uncertaintyPenaltyPerMagnitude * *frame.zeroPointStandardUncertaintyMagnitudes)
                : 1.0;
        double alignment = 1.0;
        if (medianRms > 0.0 && frame.astrometricRootMeanSquarePixels.has_value() &&
            *frame.astrometricRootMeanSquarePixels > medianRms)
        {
            const double excess = *frame.astrometricRootMeanSquarePixels / medianRms - 1.0;
            alignment = 1.0 / (1.0 + request.alignmentPenalty * excess * excess);
        }
        raw[index] = (std::max)(transparency * confidence * alignment, request.minimumRawWeight);
        measuredRaw.push_back(raw[index]);
    }
    std::sort(measuredRaw.begin(), measuredRaw.end());
    const double medianRaw =
        measuredRaw.size() % 2u != 0u
            ? measuredRaw[measuredRaw.size() / 2u]
            : (measuredRaw[measuredRaw.size() / 2u - 1u] + measuredRaw[measuredRaw.size() / 2u]) * 0.5;
    double sum = 0.0;
    for (std::size_t index = 0u; index < measurements.size(); ++index)
    {
        if (!measurements[index].zeroPointMagnitudes.has_value())
        {
            raw[index] = medianRaw;
        }
        sum += raw[index];
    }
    const double mean = sum / static_cast<double>(raw.size());
    for (double& value : raw)
    {
        value /= mean;
    }
    return raw;
}

[[nodiscard]] an::PhotometricStackWeight sentinelWeight()
{
    return an::PhotometricStackWeight{
        .value = 42.0, .standardUncertainty = 7.0, .usedMedianSubstitution = true, .minimumApplied = true};
}

void expectSentinel(const an::PhotometricStackWeight& weight)
{
    EXPECT_EQ(weight.value, 42.0);
    ASSERT_TRUE(weight.standardUncertainty.has_value());
    EXPECT_EQ(*weight.standardUncertainty, 7.0);
    EXPECT_TRUE(weight.usedMedianSubstitution);
    EXPECT_TRUE(weight.minimumApplied);
}

/// A sink that counts warnings without retaining borrowed strings.
class CountingDiagnostics final : public an::DiagnosticSink
{
public:
    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        ++events;
        if (event.severity == an::DiagnosticSeverity::Warning)
        {
            ++warnings;
        }
        operationMatches = std::strcmp(event.operation, "an::computePhotometricStackWeights") == 0;
    }

    std::uint32_t events = 0u;
    std::uint32_t warnings = 0u;
    bool operationMatches = false;
};

TEST(PhotometricWeightingTest, DispositionVocabularyIsClosedAndStable)
{
    EXPECT_TRUE(an::isPhotometricWeightingDisposition(an::PhotometricWeightingDisposition::Computed));
    EXPECT_TRUE(an::isPhotometricWeightingDisposition(an::PhotometricWeightingDisposition::SingleFrame));
    EXPECT_TRUE(an::isPhotometricWeightingDisposition(an::PhotometricWeightingDisposition::UniformNoZeroPoints));
    EXPECT_TRUE(an::isPhotometricWeightingDisposition(an::PhotometricWeightingDisposition::UniformDegenerate));
    EXPECT_FALSE(an::isPhotometricWeightingDisposition(static_cast<an::PhotometricWeightingDisposition>(99u)));
    EXPECT_STREQ(an::photometricWeightingDispositionName(an::PhotometricWeightingDisposition::Computed), "Computed");
    EXPECT_STREQ(an::photometricWeightingDispositionName(an::PhotometricWeightingDisposition::SingleFrame),
                 "SingleFrame");
    EXPECT_STREQ(an::photometricWeightingDispositionName(an::PhotometricWeightingDisposition::UniformNoZeroPoints),
                 "UniformNoZeroPoints");
    EXPECT_STREQ(an::photometricWeightingDispositionName(an::PhotometricWeightingDisposition::UniformDegenerate),
                 "UniformDegenerate");
    EXPECT_STREQ(an::photometricWeightingDispositionName(static_cast<an::PhotometricWeightingDisposition>(99u)),
                 "UnknownPhotometricWeightingDisposition");
}

TEST(PhotometricWeightingTest, MatchesTheWrittenOutModelAndProducesStackReadyWeights)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::PhotometricFrameMeasurement> measurements{{.zeroPointMagnitudes = 24.0,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.0,
                                                                     .astrometricRootMeanSquarePixels = std::nullopt},
                                                                    {.zeroPointMagnitudes = 25.0,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.1,
                                                                     .astrometricRootMeanSquarePixels = 1.0},
                                                                    {.zeroPointMagnitudes = 25.5,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.2,
                                                                     .astrometricRootMeanSquarePixels = 2.0},
                                                                    {}};
    std::vector<an::PhotometricStackWeight> output(measurements.size());
    const an::Result<an::PhotometricWeightingResult> result =
        an::computePhotometricStackWeights(execution, measurements, {}, output);
    ASSERT_TRUE(result.hasValue()) << result.status().message();

    const std::vector<double> expected = oracleWeights(measurements);
    ASSERT_EQ(output.size(), expected.size());
    for (std::size_t index = 0u; index < output.size(); ++index)
    {
        EXPECT_NEAR(output[index].value, expected[index], 1.0e-14);
        ASSERT_TRUE(output[index].standardUncertainty.has_value());
        EXPECT_TRUE(std::isfinite(*output[index].standardUncertainty));
        EXPECT_GE(*output[index].standardUncertainty, 0.0);
    }
    EXPECT_FALSE(output[0].usedMedianSubstitution);
    EXPECT_FALSE(output[1].usedMedianSubstitution);
    EXPECT_FALSE(output[2].usedMedianSubstitution);
    EXPECT_TRUE(output[3].usedMedianSubstitution);
    EXPECT_EQ(result->disposition, an::PhotometricWeightingDisposition::Computed);
    EXPECT_EQ(result->frameCount, 4u);
    EXPECT_EQ(result->measuredFrameCount, 3u);
    EXPECT_EQ(result->uncertaintyMeasurementCount, 3u);
    EXPECT_EQ(result->alignmentMeasurementCount, 2u);
    EXPECT_EQ(result->medianSubstitutionCount, 1u);
    EXPECT_EQ(result->minimumAppliedCount, 0u);
    ASSERT_TRUE(result->baselineZeroPointMagnitudes.has_value());
    EXPECT_EQ(*result->baselineZeroPointMagnitudes, 24.0);
    ASSERT_TRUE(result->medianAstrometricRootMeanSquarePixels.has_value());
    EXPECT_EQ(*result->medianAstrometricRootMeanSquarePixels, 1.5);
    EXPECT_TRUE(result->uncertaintyComplete);
    EXPECT_NEAR(result->weightSum, 4.0, 1.0e-14);
    EXPECT_GT(result->effectiveFrameCount, 1.0);
    EXPECT_LE(result->effectiveFrameCount, 4.0);
    EXPECT_GT(result->workingBytes, 0u);
}

TEST(PhotometricWeightingTest, PropagatedUncertaintyMatchesFiniteDifferences)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::PhotometricFrameMeasurement> measurements{{.zeroPointMagnitudes = 20.0,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.015,
                                                                     .astrometricRootMeanSquarePixels = 0.9},
                                                                    {.zeroPointMagnitudes = 20.7,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.025,
                                                                     .astrometricRootMeanSquarePixels = 1.0},
                                                                    {.zeroPointMagnitudes = 21.3,
                                                                     .zeroPointStandardUncertaintyMagnitudes = 0.035,
                                                                     .astrometricRootMeanSquarePixels = 1.4},
                                                                    {}};
    std::vector<an::PhotometricStackWeight> output(measurements.size());
    const an::Result<an::PhotometricWeightingResult> result =
        an::computePhotometricStackWeights(execution, measurements, {}, output);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_TRUE(result->uncertaintyComplete);

    constexpr double kStep = 1.0e-6;
    std::vector<double> variances(measurements.size(), 0.0);
    for (std::size_t source = 0u; source < 3u; ++source)
    {
        std::vector<an::PhotometricFrameMeasurement> lower = measurements;
        std::vector<an::PhotometricFrameMeasurement> upper = measurements;
        *lower[source].zeroPointMagnitudes -= kStep;
        *upper[source].zeroPointMagnitudes += kStep;
        std::vector<an::PhotometricStackWeight> lowerOutput(measurements.size());
        std::vector<an::PhotometricStackWeight> upperOutput(measurements.size());
        const auto lowerResult = an::computePhotometricStackWeights(execution, lower, {}, lowerOutput);
        const auto upperResult = an::computePhotometricStackWeights(execution, upper, {}, upperOutput);
        ASSERT_TRUE(lowerResult.hasValue()) << lowerResult.status().message();
        ASSERT_TRUE(upperResult.hasValue()) << upperResult.status().message();
        const double sigma = *measurements[source].zeroPointStandardUncertaintyMagnitudes;
        for (std::size_t destination = 0u; destination < output.size(); ++destination)
        {
            const double derivative = (upperOutput[destination].value - lowerOutput[destination].value) / (2.0 * kStep);
            variances[destination] += derivative * derivative * sigma * sigma;
        }
    }
    for (std::size_t index = 0u; index < output.size(); ++index)
    {
        ASSERT_TRUE(output[index].standardUncertainty.has_value());
        EXPECT_NEAR(*output[index].standardUncertainty, std::sqrt(variances[index]), 2.0e-9);
    }
}

TEST(PhotometricWeightingTest, MissingModifiersAreNeutralAndMissingZeroPointsUseTheMedian)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::PhotometricWeightingRequest request;
    request.minimumRawWeight = 0.5;
    const std::vector<an::PhotometricFrameMeasurement> measurements{
        {.zeroPointMagnitudes = 20.0},
        {.zeroPointMagnitudes = 20.0,
         .zeroPointStandardUncertaintyMagnitudes = 100.0,
         .astrometricRootMeanSquarePixels = 1.0},
        {.zeroPointMagnitudes = 20.0, .astrometricRootMeanSquarePixels = 4.0},
        {}};
    std::vector<an::PhotometricStackWeight> output(measurements.size());
    const auto result = an::computePhotometricStackWeights(execution, measurements, request, output);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    const std::vector<double> expected = oracleWeights(measurements, request);
    for (std::size_t index = 0u; index < output.size(); ++index)
    {
        EXPECT_NEAR(output[index].value, expected[index], 1.0e-14);
        EXPECT_FALSE(output[index].standardUncertainty.has_value());
    }
    EXPECT_TRUE(output[1].minimumApplied);
    EXPECT_TRUE(output[2].minimumApplied);
    EXPECT_TRUE(output[3].usedMedianSubstitution);
    EXPECT_EQ(result->minimumAppliedCount, 2u);
    EXPECT_EQ(result->medianSubstitutionCount, 1u);
    EXPECT_FALSE(result->uncertaintyComplete);
}

TEST(PhotometricWeightingTest, UniformAndSingleFrameCasesAreExplicit)
{
    CountingDiagnostics diagnostics;
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine, 4u, {}, &diagnostics);
    const std::vector<an::PhotometricFrameMeasurement> missing(3u);
    std::vector<an::PhotometricStackWeight> uniform(3u);
    const auto noPoints = an::computePhotometricStackWeights(execution, missing, {}, uniform);
    ASSERT_TRUE(noPoints.hasValue()) << noPoints.status().message();
    EXPECT_EQ(noPoints->disposition, an::PhotometricWeightingDisposition::UniformNoZeroPoints);
    EXPECT_EQ(noPoints->measuredFrameCount, 0u);
    EXPECT_FALSE(noPoints->uncertaintyComplete);
    EXPECT_EQ(noPoints->weightSum, 3.0);
    EXPECT_EQ(noPoints->effectiveFrameCount, 3.0);
    for (const an::PhotometricStackWeight& weight : uniform)
    {
        EXPECT_EQ(weight.value, 1.0);
        EXPECT_FALSE(weight.standardUncertainty.has_value());
    }
    EXPECT_EQ(diagnostics.events, 1u);
    EXPECT_EQ(diagnostics.warnings, 1u);
    EXPECT_TRUE(diagnostics.operationMatches);

    an::PhotometricWeightingRequest degenerateRequest;
    degenerateRequest.uncertaintyPenaltyPerMagnitude = (std::numeric_limits<double>::max)();
    degenerateRequest.minimumRawWeight = 0.0;
    const std::vector<an::PhotometricFrameMeasurement> degenerateMeasurements{
        {.zeroPointMagnitudes = 22.0, .zeroPointStandardUncertaintyMagnitudes = (std::numeric_limits<double>::max)()},
        {.zeroPointMagnitudes = 23.0, .zeroPointStandardUncertaintyMagnitudes = (std::numeric_limits<double>::max)()}};
    std::vector<an::PhotometricStackWeight> degenerate(degenerateMeasurements.size(), sentinelWeight());
    const auto degenerateResult =
        an::computePhotometricStackWeights(execution, degenerateMeasurements, degenerateRequest, degenerate);
    ASSERT_TRUE(degenerateResult.hasValue()) << degenerateResult.status().message();
    EXPECT_EQ(degenerateResult->disposition, an::PhotometricWeightingDisposition::UniformDegenerate);
    EXPECT_FALSE(degenerateResult->uncertaintyComplete);
    EXPECT_EQ(degenerateResult->weightSum, 2.0);
    EXPECT_EQ(degenerateResult->effectiveFrameCount, 2.0);
    for (const an::PhotometricStackWeight& weight : degenerate)
    {
        EXPECT_EQ(weight.value, 1.0);
        EXPECT_FALSE(weight.standardUncertainty.has_value());
        EXPECT_FALSE(weight.usedMedianSubstitution);
        EXPECT_FALSE(weight.minimumApplied);
    }
    EXPECT_EQ(diagnostics.events, 2u);
    EXPECT_EQ(diagnostics.warnings, 2u);

    const std::vector<an::PhotometricFrameMeasurement> singleton{{.zeroPointMagnitudes = 22.5,
                                                                  .zeroPointStandardUncertaintyMagnitudes = 0.02,
                                                                  .astrometricRootMeanSquarePixels = 0.8}};
    std::vector<an::PhotometricStackWeight> single(1u);
    const auto one = an::computePhotometricStackWeights(execution, singleton, {}, single);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    EXPECT_EQ(one->disposition, an::PhotometricWeightingDisposition::SingleFrame);
    EXPECT_EQ(one->weightSum, 1.0);
    EXPECT_EQ(one->effectiveFrameCount, 1.0);
    EXPECT_EQ(one->workingBytes, 0u);
    ASSERT_TRUE(single[0].standardUncertainty.has_value());
    EXPECT_EQ(*single[0].standardUncertainty, 0.0);
}

TEST(PhotometricWeightingTest, RejectsMalformedAndUnrepresentableInputsWithoutWritingOutput)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::PhotometricFrameMeasurement> valid{{.zeroPointMagnitudes = 20.0},
                                                             {.zeroPointMagnitudes = 21.0}};
    std::vector<an::PhotometricStackWeight> output(valid.size(), sentinelWeight());

    const an::ExecutionContext emptyExecution;
    EXPECT_EQ(an::computePhotometricStackWeights(emptyExecution, valid, {}, output).status().code(),
              an::ErrorCode::NotInitialized);
    for (const auto& weight : output)
    {
        expectSentinel(weight);
    }
    EXPECT_EQ(an::computePhotometricStackWeights(execution, {}, {}, {}).status().code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::computePhotometricStackWeights(execution, valid, {}, std::span{output}.first(1u)).status().code(),
              an::ErrorCode::IncompatibleArguments);

    an::PhotometricWeightingRequest badRequest;
    badRequest.uncertaintyPenaltyPerMagnitude = -1.0;
    EXPECT_EQ(an::computePhotometricStackWeights(execution, valid, badRequest, output).status().code(),
              an::ErrorCode::InvalidArgument);
    badRequest = {};
    badRequest.alignmentPenalty = (std::numeric_limits<double>::infinity)();
    EXPECT_EQ(an::computePhotometricStackWeights(execution, valid, badRequest, output).status().code(),
              an::ErrorCode::InvalidArgument);
    badRequest = {};
    badRequest.minimumRawWeight = 1.01;
    EXPECT_EQ(an::computePhotometricStackWeights(execution, valid, badRequest, output).status().code(),
              an::ErrorCode::InvalidArgument);

    std::vector<an::PhotometricFrameMeasurement> bad = valid;
    bad[0].zeroPointMagnitudes = (std::numeric_limits<double>::quiet_NaN)();
    EXPECT_EQ(an::computePhotometricStackWeights(execution, bad, {}, output).status().code(),
              an::ErrorCode::InvalidArgument);
    bad = valid;
    bad[0].zeroPointStandardUncertaintyMagnitudes = -0.1;
    EXPECT_EQ(an::computePhotometricStackWeights(execution, bad, {}, output).status().code(),
              an::ErrorCode::InvalidArgument);
    bad = valid;
    bad[0].astrometricRootMeanSquarePixels = 0.0;
    EXPECT_EQ(an::computePhotometricStackWeights(execution, bad, {}, output).status().code(),
              an::ErrorCode::InvalidArgument);
    bad = valid;
    bad[0].zeroPointMagnitudes = std::nullopt;
    bad[0].zeroPointStandardUncertaintyMagnitudes = 0.1;
    EXPECT_EQ(an::computePhotometricStackWeights(execution, bad, {}, output).status().code(),
              an::ErrorCode::IncompatibleArguments);
    bad = {{.zeroPointMagnitudes = -(std::numeric_limits<double>::max)()},
           {.zeroPointMagnitudes = (std::numeric_limits<double>::max)()}};
    EXPECT_EQ(an::computePhotometricStackWeights(execution, bad, {}, output).status().code(),
              an::ErrorCode::InvalidData);
    for (const auto& weight : output)
    {
        expectSentinel(weight);
    }
}

TEST(PhotometricWeightingTest, HonorsTheFrameCeilingMemoryBudgetAndCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::PhotometricFrameMeasurement> tooMany(an::kMaxPhotometricWeightFrames + 1u);
    std::vector<an::PhotometricStackWeight> tooManyOutput(tooMany.size());
    EXPECT_EQ(an::computePhotometricStackWeights(execution, tooMany, {}, tooManyOutput).status().code(),
              an::ErrorCode::LimitExceeded);

    const std::vector<an::PhotometricFrameMeasurement> measurements{{.zeroPointMagnitudes = 20.0},
                                                                    {.zeroPointMagnitudes = 21.0}};
    std::vector<an::PhotometricStackWeight> output(measurements.size(), sentinelWeight());
    const an::EngineContext tinyEngine = makeEngine(1u, 1u);
    const an::ExecutionContext tinyExecution = makeExecution(tinyEngine, 1u);
    EXPECT_EQ(an::computePhotometricStackWeights(tinyExecution, measurements, {}, output).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    for (const auto& weight : output)
    {
        expectSentinel(weight);
    }

    std::stop_source stop;
    stop.request_stop();
    const an::EngineContext stoppedEngine = makeEngine();
    const an::ExecutionContext stopped = makeExecution(stoppedEngine, 4u, stop.get_token());
    EXPECT_EQ(an::computePhotometricStackWeights(stopped, measurements, {}, output).status().code(),
              an::ErrorCode::Cancelled);
    for (const auto& weight : output)
    {
        expectSentinel(weight);
    }
}

TEST(PhotometricWeightingTest, ResultsDoNotDependOnAdmittedConcurrency)
{
    std::vector<an::PhotometricFrameMeasurement> measurements(1025u);
    for (std::uint32_t index = 0u; index < measurements.size(); ++index)
    {
        if (index % 13u == 0u)
        {
            continue;
        }
        measurements[index].zeroPointMagnitudes = 20.0 + static_cast<double>((index * 17u) % 101u) * 0.01;
        measurements[index].zeroPointStandardUncertaintyMagnitudes =
            0.01 + static_cast<double>((index * 7u) % 19u) * 0.001;
        if (index % 5u != 0u)
        {
            measurements[index].astrometricRootMeanSquarePixels = 0.7 + static_cast<double>((index * 11u) % 31u) * 0.02;
        }
    }
    const an::EngineContext serialEngine = makeEngine(1u);
    const an::ExecutionContext serialExecution = makeExecution(serialEngine, 1u);
    const an::EngineContext parallelEngine = makeEngine(4u);
    const an::ExecutionContext parallelExecution = makeExecution(parallelEngine, 4u);
    std::vector<an::PhotometricStackWeight> serial(measurements.size());
    std::vector<an::PhotometricStackWeight> parallel(measurements.size());
    const auto serialResult = an::computePhotometricStackWeights(serialExecution, measurements, {}, serial);
    const auto parallelResult = an::computePhotometricStackWeights(parallelExecution, measurements, {}, parallel);
    ASSERT_TRUE(serialResult.hasValue()) << serialResult.status().message();
    ASSERT_TRUE(parallelResult.hasValue()) << parallelResult.status().message();
    EXPECT_EQ(serialResult->weightSum, parallelResult->weightSum);
    EXPECT_EQ(serialResult->effectiveFrameCount, parallelResult->effectiveFrameCount);
    EXPECT_EQ(serialResult->workingBytes, parallelResult->workingBytes);
    for (std::size_t index = 0u; index < serial.size(); ++index)
    {
        EXPECT_EQ(serial[index].value, parallel[index].value);
        EXPECT_EQ(serial[index].standardUncertainty, parallel[index].standardUncertainty);
        EXPECT_EQ(serial[index].usedMedianSubstitution, parallel[index].usedMedianSubstitution);
        EXPECT_EQ(serial[index].minimumApplied, parallel[index].minimumApplied);
    }
}

} // namespace
