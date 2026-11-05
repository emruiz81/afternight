// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the two patch denoisers.
//
// Three themes run through the suite, and each of them is a defect a patch denoiser is easy to have.
//
// A noise level is a number in the image's own units: the same request over the same picture stored
// two ways has to mean the same thing, which a normalise-and-multiply-by-255 does not deliver. The
// tiling is a property of the image: the same frame denoised on one thread and on
// four has to give the same answer, which a division into as many pieces as the host has cores does
// not deliver. And a run says what it did: how much noise came in and went out, how many groups were
// formed, how many covariance inversions failed, and whether the strengthening loop was abandoned.

#include <AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include "SpatialFilterFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using spatial_test::makeEngine;
using spatial_test::makeExecution;
using spatial_test::planarLayout;
using spatial_test::Sequence;

static_assert(!std::is_constructible_v<an::PatchDenoisePlan, const an::PatchDenoiseRequest&, std::uint32_t>);
static_assert(!std::is_constructible_v<an::GuidedShrinkagePlan, const an::GuidedShrinkageRequest&>);

/// A frame whose samples are a deterministic sequence around a stated level.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many channels.
/// @param level the value the samples sit around.
/// @param amplitude how far they wander from it.
/// @param seed which sequence to draw from.
/// @return the samples, channel-major and tightly packed.
[[nodiscard]] std::vector<float> noisyFrame(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                            float level, float amplitude, std::uint64_t seed)
{
    Sequence sequence{seed};
    std::vector<float> samples(static_cast<std::size_t>(width) * height * channels, 0.0f);
    for (float& sample : samples)
    {
        sample = level + amplitude * (sequence.next() - 0.5f);
    }
    return samples;
}

/// A frame holding one value everywhere.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many channels.
/// @param value the value.
/// @return the samples.
[[nodiscard]] std::vector<float> constantFrame(std::uint32_t width, std::uint32_t height, std::uint32_t channels,
                                               float value)
{
    return std::vector<float>(static_cast<std::size_t>(width) * height * channels, value);
}

/// A denoise request that runs without measuring anything.
///
/// @param sigma the noise level in the image's own units.
/// @param whiteLevel what a fully exposed sample is.
/// @return the request.
[[nodiscard]] an::PatchDenoiseRequest plainRequest(double sigma, double whiteLevel)
{
    an::PatchDenoiseRequest request;
    request.sigma = sigma;
    request.whiteLevel = whiteLevel;
    return request;
}

TEST(PatchDenoiseTest, APlanRefusesTheRequestsThatCannotMeanAnything)
{
    an::PatchDenoiseRequest request = plainRequest(0.02, 1.0);
    EXPECT_TRUE(an::planPatchDenoise(request, 1u).hasValue());

    an::PatchDenoiseRequest noScale = request;
    noScale.whiteLevel = 0.0;
    EXPECT_EQ(an::planPatchDenoise(noScale, 1u).status().code(), an::ErrorCode::InvalidArgument);

    an::PatchDenoiseRequest noSigma = request;
    noSigma.sigma = 0.0;
    EXPECT_EQ(an::planPatchDenoise(noSigma, 1u).status().code(), an::ErrorCode::InvalidArgument);

    an::PatchDenoiseRequest tooLoud = request;
    tooLoud.sigma = 10.0;
    EXPECT_EQ(an::planPatchDenoise(tooLoud, 1u).status().code(), an::ErrorCode::OutOfRange);

    an::PatchDenoiseRequest tooManyIterations = request;
    tooManyIterations.iterations = an::kMaximumPatchDenoiseIterations + 1u;
    EXPECT_EQ(an::planPatchDenoise(tooManyIterations, 1u).status().code(), an::ErrorCode::OutOfRange);

    an::PatchDenoiseRequest badMix = request;
    badMix.iterations = 2u;
    badMix.reinjection = 1.0;
    EXPECT_EQ(an::planPatchDenoise(badMix, 1u).status().code(), an::ErrorCode::InvalidArgument);

    an::PatchDenoiseRequest fullScale = request;
    fullScale.output.writeBack.domain = an::DomainMapping::FullScale;
    EXPECT_EQ(an::planPatchDenoise(fullScale, 1u).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(PatchDenoiseTest, ATwoChannelFrameIsRefusedByNameRatherThanDecorrelatedWrongly)
{
    // The published colour decorrelation is written for one, three or four channels. A two-channel
    // frame is refused with the reason in the code, rather than with a failure code a caller has to
    // turn into an exception.
    const an::PatchDenoiseRequest request = plainRequest(0.02, 1.0);
    EXPECT_EQ(an::planPatchDenoise(request, 2u).status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::planPatchDenoise(request, 5u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_TRUE(an::planPatchDenoise(request, 1u).hasValue());
    EXPECT_TRUE(an::planPatchDenoise(request, 3u).hasValue());
    EXPECT_TRUE(an::planPatchDenoise(request, 4u).hasValue());
}

TEST(PatchDenoiseTest, VarianceStabilisationRefusesTheTwoThingsItCannotBeCombinedWith)
{
    // Silently overriding the caller and writing a log line would hide either combination. A request
    // that cannot be honoured is refused here, because a caller who asked for both wants to know it
    // got neither.
    an::PatchDenoiseRequest looping = plainRequest(0.02, 1.0);
    looping.varianceStabilisation = true;
    looping.iterations = 3u;
    EXPECT_EQ(an::planPatchDenoise(looping, 1u).status().code(), an::ErrorCode::IncompatibleArguments);

    an::PatchDenoiseRequest finishing = plainRequest(0.02, 1.0);
    finishing.varianceStabilisation = true;
    finishing.finishing = an::PatchDenoiseFinishing::GuidedDualDomain;
    EXPECT_EQ(an::planPatchDenoise(finishing, 1u).status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(PatchDenoiseTest, ThePassParametersFollowTheMethodsOwnTables)
{
    const an::PatchDenoiseRequest request = plainRequest(0.02, 1.0);
    const an::Result<an::PatchDenoisePlan> monochrome = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(monochrome.hasValue());

    // A quiet monochrome frame: five-sample patches, thirty-five neighbours on the first pass and
    // fifteen on the second, with the search window derived as the odd number at or above half the
    // neighbour count.
    const an::PatchDenoisePassParameters first = monochrome->passParameters(5.0, false);
    EXPECT_EQ(first.patchSize, 5u);
    EXPECT_EQ(first.similarPatches, 35u);
    EXPECT_EQ(first.searchWindow, 17u);
    EXPECT_EQ(first.referenceStep, 2u);
    EXPECT_EQ(first.boundary, 25u);
    EXPECT_FLOAT_EQ(first.covarianceWeight, 1.1f);
    EXPECT_FLOAT_EQ(first.similarityDistance, 0.0f);

    const an::PatchDenoisePassParameters second = monochrome->passParameters(5.0, true);
    EXPECT_EQ(second.patchSize, 5u);
    EXPECT_EQ(second.similarPatches, 15u);
    EXPECT_EQ(second.searchWindow, 7u);
    EXPECT_FLOAT_EQ(second.similarityDistance, 16.0f * 25.0f);

    // A loud monochrome frame moves to seven-sample patches and a hundred neighbours.
    const an::PatchDenoisePassParameters loud = monochrome->passParameters(90.0, false);
    EXPECT_EQ(loud.patchSize, 7u);
    EXPECT_EQ(loud.similarPatches, 100u);
    EXPECT_FLOAT_EQ(loud.covarianceWeight, 0.9f);

    EXPECT_EQ(monochrome->passParameters((std::numeric_limits<double>::max)(), false).patchSize, 0u);

    // A colour frame keeps three times the patch area as its neighbour count.
    const an::Result<an::PatchDenoisePlan> colour = an::planPatchDenoise(request, 3u);
    ASSERT_TRUE(colour.hasValue());
    const an::PatchDenoisePassParameters colourFirst = colour->passParameters(5.0, false);
    EXPECT_EQ(colourFirst.patchSize, 3u);
    EXPECT_EQ(colourFirst.similarPatches, 27u);
    EXPECT_EQ(colourFirst.searchWindow, 13u);
    EXPECT_FLOAT_EQ(colourFirst.covarianceWeight, 1.0f);
}

TEST(PatchDenoiseTest, TheTuningScaleIsWhatTheCallerStatedRatherThanWhatTheFrameHolds)
{
    // A working maximum derived from the largest sample of a floating-point frame would make the
    // same request mean different things for two frames of the same picture at different exposures.
    // Here the scale is the two numbers the caller stated.
    const an::Result<an::PatchDenoisePlan> unit = an::planPatchDenoise(plainRequest(0.02, 1.0), 1u);
    const an::Result<an::PatchDenoisePlan> sixteenBit = an::planPatchDenoise(plainRequest(1310.7, 65535.0), 1u);
    ASSERT_TRUE(unit.hasValue());
    ASSERT_TRUE(sixteenBit.hasValue());
    EXPECT_DOUBLE_EQ(unit->tuningScale(), 255.0);
    EXPECT_NEAR(sixteenBit->tuningScale(), 255.0 / 65535.0, 1.0e-12);
    // Both requests describe the same fraction of full scale, so both select the same tuning.
    EXPECT_EQ(unit->passParameters(0.02 * unit->tuningScale(), false).patchSize,
              sixteenBit->passParameters(1310.7 * sixteenBit->tuningScale(), false).patchSize);
}

TEST(PatchDenoiseTest, TheTileGridIsAFunctionOfTheFrameAndItsBoundary)
{
    // Nothing here reads a thread count, which is the whole point: two hosts denoising one frame
    // divide it the same way.
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    an::PatchDenoisePlan::tileGrid(64u, 64u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 1u);
    EXPECT_EQ(tilesY, 1u);

    an::PatchDenoisePlan::tileGrid(4096u, 2048u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 16u);
    EXPECT_EQ(tilesY, 8u);

    // A boundary wide enough to dominate a tile widens the tiles rather than multiplying them.
    an::PatchDenoisePlan::tileGrid(4096u, 4096u, 400u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 2u);
    EXPECT_EQ(tilesY, 2u);
    EXPECT_LE(static_cast<std::uint64_t>(tilesX) * tilesY, an::kMaximumPatchDenoiseTiles);
}

TEST(PatchDenoiseTest, AFrameTooSmallForSixteenTilesTakesSmallerOnesDownToFourBoundaries)
{
    // At the preferred interior of 256 samples a 512-sample frame holds four tiles, and a pass over it
    // can keep only four workers busy. Its tiles shrink to the largest interior that gives sixteen.
    std::uint32_t tilesX = 0u;
    std::uint32_t tilesY = 0u;
    an::PatchDenoisePlan::tileGrid(512u, 512u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 4u);
    EXPECT_EQ(tilesY, 4u);
    an::PatchDenoisePlan::tileGrid(512u, 512u, 10u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 4u);
    EXPECT_EQ(tilesY, 4u);

    // The largest interior that reaches sixteen: a strip reaches them along its length.
    an::PatchDenoisePlan::tileGrid(2048u, 256u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 16u);
    EXPECT_EQ(tilesY, 2u);

    // Four boundaries is as small as an interior gets, so a frame that cannot hold sixteen of those
    // holds as many as it can.
    an::PatchDenoisePlan::tileGrid(300u, 300u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 3u);
    EXPECT_EQ(tilesY, 3u);

    // A frame that already holds sixteen at the preferred interior keeps its grid.
    an::PatchDenoisePlan::tileGrid(1024u, 1024u, 25u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 4u);
    EXPECT_EQ(tilesY, 4u);
    an::PatchDenoisePlan::tileGrid(1100u, 1030u, 10u, tilesX, tilesY);
    EXPECT_EQ(tilesX, 4u);
    EXPECT_EQ(tilesY, 4u);
}

TEST(PatchDenoiseTest, TheGuidedStageDividesASmallFrameIntoSixteenTilesDownToHalfATransformOfBorder)
{
    // The guided stage carries half a transform of border on each side of a tile, so its smallest
    // interior is two transforms; at the default radius that is 128 samples.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(an::GuidedShrinkageRequest{.sigma = 0.02});
    ASSERT_TRUE(plan.hasValue());
    ASSERT_EQ(plan->transformSize(), 64u);
    for (const auto& [side, tiles] : {std::pair<std::uint32_t, std::uint32_t>{512u, 16u}, {320u, 4u}})
    {
        const std::vector<float> noisy = noisyFrame(side, side, 1u, 0.5f, 0.1f, 5u);
        const std::vector<float> guide = constantFrame(side, side, 1u, 0.5f);
        const an::ImageLayout layout = planarLayout(an::SampleType::Float32, side, side);
        std::vector<float> answer(noisy.size(), 0.0f);
        const an::Result<an::GuidedShrinkageResult> run =
            an::shrinkImageWithGuide(execution, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout,
                                     answer.data(), layout, *plan);
        ASSERT_TRUE(run.hasValue()) << run.status().message();
        EXPECT_EQ(run->tileCount, tiles) << side;
    }
}

TEST(PatchDenoiseTest, TheNoiseEstimateIsTheRowDifferenceDeviationOverTheSquareRootOfTwo)
{
    // A frame whose rows alternate between two levels has first differences of exactly plus and
    // minus the step, so its clipped deviation is the step and the reported estimate is the step
    // over the square root of two. Measuring it against an exact answer is what makes the
    // estimator's units checkable at all.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    // An odd width leaves an even number of first differences, so the alternating steps cancel and
    // the clipped mean is exactly zero - which is what makes the expected deviation exactly the step
    // rather than the step times a factor of the row length.
    constexpr std::uint32_t kWidth = 65u;
    constexpr std::uint32_t kHeight = 32u;
    constexpr float kStep = 0.25f;
    std::vector<float> samples(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t i = 0u; i < samples.size(); ++i)
    {
        samples[i] = (i % 2u == 0u) ? 0.5f : 0.5f + kStep;
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<double> measured =
        an::estimateImageNoise(execution, an::SampleType::Float32, samples.data(), layout);
    ASSERT_TRUE(measured.hasValue());
    EXPECT_NEAR(*measured, static_cast<double>(kStep) / std::sqrt(2.0), 1.0e-6);
}

TEST(PatchDenoiseTest, TheNoiseEstimateNeedsARowItCanTakeDifferencesAlong)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<float> samples(8u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 2u, 4u);
    EXPECT_EQ(an::estimateImageNoise(execution, an::SampleType::Float32, samples.data(), layout).status().code(),
              an::ErrorCode::OutOfRange);
}

TEST(PatchDenoiseTest, ARequestMeansTheSameThingWhateverTheStorage)
{
    // The regression the whole domain is arranged around. The two frames hold the same code values
    // and the request states its noise level and its full scale in those code values, so the two
    // answers have to agree to within the destination representation's own rounding.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;

    Sequence sequence{7u};
    std::vector<std::uint16_t> integerSource(static_cast<std::size_t>(kWidth) * kHeight, 0u);
    std::vector<float> floatSource(integerSource.size(), 0.0f);
    for (std::size_t i = 0u; i < integerSource.size(); ++i)
    {
        const auto code = static_cast<std::uint16_t>(20000.0f + 4000.0f * (sequence.next() - 0.5f));
        integerSource[i] = code;
        floatSource[i] = static_cast<float>(code);
    }

    const an::PatchDenoiseRequest request = plainRequest(1200.0, 65535.0);
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());

    const an::ImageLayout integerLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight);
    const an::ImageLayout floatLayout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    std::vector<std::uint16_t> integerAnswer(integerSource.size(), 0u);
    std::vector<float> floatAnswer(floatSource.size(), 0.0f);

    const an::Result<an::PatchDenoiseResult> integerRun =
        an::denoiseImagePatches(execution, an::SampleType::UInt16, integerSource.data(), integerLayout,
                                integerAnswer.data(), integerLayout, *plan);
    ASSERT_TRUE(integerRun.hasValue()) << integerRun.status().message();
    const an::Result<an::PatchDenoiseResult> floatRun = an::denoiseImagePatches(
        execution, an::SampleType::Float32, floatSource.data(), floatLayout, floatAnswer.data(), floatLayout, *plan);
    ASSERT_TRUE(floatRun.hasValue()) << floatRun.status().message();

    for (std::size_t i = 0u; i < integerAnswer.size(); ++i)
    {
        EXPECT_NEAR(static_cast<double>(integerAnswer[i]), static_cast<double>(floatAnswer[i]), 1.0) << "sample " << i;
    }
    EXPECT_NEAR(integerRun->inputNoise, floatRun->inputNoise, 1.0);
}

TEST(PatchDenoiseTest, TheAnswerDoesNotDependOnHowManyThreadsRanIt)
{
    // A frame divided into twice the host's core count fails this comparison by construction. Here
    // the grid comes from the extent and the answer is bitwise equal.
    const an::EngineContext wide = makeEngine(8u);
    const an::EngineContext narrow = makeEngine(8u);
    const an::ExecutionContext eight = makeExecution(wide, 8u);
    const an::ExecutionContext one = makeExecution(narrow, 1u);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 96u;
    const std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.2f, 11u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(plainRequest(0.03, 1.0), 1u);
    ASSERT_TRUE(plan.hasValue());

    std::vector<float> wideAnswer(source.size(), 0.0f);
    std::vector<float> narrowAnswer(source.size(), 0.0f);
    ASSERT_TRUE(
        an::denoiseImagePatches(eight, an::SampleType::Float32, source.data(), layout, wideAnswer.data(), layout, *plan)
            .hasValue());
    ASSERT_TRUE(
        an::denoiseImagePatches(one, an::SampleType::Float32, source.data(), layout, narrowAnswer.data(), layout, *plan)
            .hasValue());
    EXPECT_EQ(wideAnswer, narrowAnswer);
}

TEST(PatchDenoiseTest, ADenoiseLowersTheMeasuredNoiseAndSaysByHowMuch)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 128u;
    const std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.2f, 3u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::PatchDenoiseRequest request = plainRequest(0.05, 1.0);
    request.measureNoise = true;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());

    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_GT(run->inputNoise, 0.0);
    EXPECT_LT(run->outputNoise, run->inputNoise);
    EXPECT_GT(run->patchGroups, 0u);
    EXPECT_EQ(run->iterationsRun, 1u);
    EXPECT_FALSE(run->convergenceStopped);
    EXPECT_GT(run->samplesChanged, 0u);
    EXPECT_GT(run->filter.workingBytes, 0u);
    EXPECT_EQ(run->filter.precision, an::ComputePrecision::Float32);
    EXPECT_NEAR(run->effectiveSigma, run->inputNoise, 1.0e-12);
}

TEST(PatchDenoiseTest, ADenoiseOfAFlatFrameLeavesItFlat)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    const std::vector<float> source = constantFrame(kWidth, kHeight, 1u, 0.375f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(plainRequest(0.02, 1.0), 1u);
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    for (const float sample : answer)
    {
        EXPECT_NEAR(sample, 0.375f, 1.0e-4f);
    }
}

TEST(PatchDenoiseTest, ARejectedSampleIsSubstitutedAndItsDestinationLeftAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.1f, 5u);
    source[static_cast<std::size_t>(40u) * kWidth + 40u] = std::numeric_limits<float>::quiet_NaN();
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::PatchDenoiseRequest request = plainRequest(0.03, 1.0);
    request.invalidSample = 0.5;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());

    an::SpatialSelection selection;
    selection.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
    std::vector<float> answer(source.size(), -1.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan, selection);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_EQ(run->substitutedSamples, 1u);
    // The rejected centre's own destination is never written, so it still holds what the caller put
    // there. Everything around it is.
    EXPECT_FLOAT_EQ(answer[static_cast<std::size_t>(40u) * kWidth + 40u], -1.0f);
    EXPECT_NE(answer[static_cast<std::size_t>(40u) * kWidth + 41u], -1.0f);
}

TEST(PatchDenoiseTest, ADenoiseStopsWhenItIsAskedTo)
{
    const an::EngineContext engine = makeEngine(4u);
    std::stop_source stop;
    an::ExecutionOptions options;
    options.maxConcurrency = 3u;
    options.stopToken = stop.get_token();
    const an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, options);
    ASSERT_TRUE(execution.hasValue());
    stop.request_stop();

    constexpr std::uint32_t kWidth = 96u;
    constexpr std::uint32_t kHeight = 96u;
    const std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.1f, 9u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(plainRequest(0.03, 1.0), 1u);
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(source.size(), -1.0f);
    EXPECT_EQ(an::denoiseImagePatches(*execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout,
                                      *plan)
                  .status()
                  .code(),
              an::ErrorCode::Cancelled);
    for (const float sample : answer)
    {
        EXPECT_FLOAT_EQ(sample, -1.0f);
    }
}

TEST(PatchDenoiseTest, AFrameSmallerThanOneSearchWindowIsRefusedRatherThanRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kSide = 12u;
    const std::vector<float> source = constantFrame(kSide, kSide, 1u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kSide, kSide);
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(plainRequest(0.02, 1.0), 1u);
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(source.size(), 0.0f);
    EXPECT_EQ(
        an::denoiseImagePatches(execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan)
            .status()
            .code(),
        an::ErrorCode::OutOfRange);
}

TEST(PatchDenoiseTest, AGuidedShrinkagePlanRefusesARadiusPastItsCeiling)
{
    an::GuidedShrinkageRequest request;
    request.sigma = 0.02;
    EXPECT_TRUE(an::planGuidedShrinkage(request).hasValue());

    an::GuidedShrinkageRequest wide = request;
    wide.radius = an::kMaximumGuidedShrinkageRadius + 1u;
    EXPECT_EQ(an::planGuidedShrinkage(wide).status().code(), an::ErrorCode::OutOfRange);

    an::GuidedShrinkageRequest zero = request;
    zero.radius = 0u;
    EXPECT_EQ(an::planGuidedShrinkage(zero).status().code(), an::ErrorCode::OutOfRange);

    an::GuidedShrinkageRequest silent = request;
    silent.sigma = 0.0;
    EXPECT_EQ(an::planGuidedShrinkage(silent).status().code(), an::ErrorCode::InvalidArgument);

    an::GuidedShrinkageRequest unrepresentableSigma = request;
    unrepresentableSigma.sigma = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::planGuidedShrinkage(unrepresentableSigma).status().code(), an::ErrorCode::OutOfRange);

    an::GuidedShrinkageRequest unrepresentableSpatial = request;
    unrepresentableSpatial.spatialSigma = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::planGuidedShrinkage(unrepresentableSpatial).status().code(), an::ErrorCode::OutOfRange);

    an::GuidedShrinkageRequest unrepresentableRange = request;
    unrepresentableRange.rangeFactor = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::planGuidedShrinkage(unrepresentableRange).status().code(), an::ErrorCode::OutOfRange);

    an::GuidedShrinkageRequest unrepresentableCoverage = request;
    unrepresentableCoverage.coverageThreshold = (std::numeric_limits<double>::max)();
    EXPECT_EQ(an::planGuidedShrinkage(unrepresentableCoverage).status().code(), an::ErrorCode::OutOfRange);
}

TEST(PatchDenoiseTest, AGuidedShrinkageDerivesThePowerOfTwoTransformItsRadiusImplies)
{
    an::GuidedShrinkageRequest request;
    request.sigma = 0.02;
    request.radius = 31u;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(plan->transformSize(), 64u);

    request.radius = 4u;
    const an::Result<an::GuidedShrinkagePlan> small = an::planGuidedShrinkage(request);
    ASSERT_TRUE(small.hasValue());
    EXPECT_EQ(small->transformSize(), 16u);
}

TEST(PatchDenoiseTest, AGuidedShrinkageOfAFlatFrameIsThatFlatFrame)
{
    // Every stage of the method is exact here: the regression plane is zero, the weighted patch is
    // its own mean, the only frequency with any power is the one the shrinkage skips, and the
    // aggregation is a weighted average of one value. Anything but the constant back is a defect.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 80u;
    constexpr std::uint32_t kHeight = 80u;
    const std::vector<float> source = constantFrame(kWidth, kHeight, 1u, 0.25f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::GuidedShrinkageRequest request;
    request.sigma = 0.02;
    request.radius = 8u;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::GuidedShrinkageResult> run = an::shrinkImageWithGuide(
        execution, an::SampleType::Float32, source.data(), layout, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_GT(run->patchesPlaced, 0u);
    // A radius of eight needs a seventeen-sample patch, and the next power of two above that is
    // thirty-two rather than sixteen.
    EXPECT_EQ(run->transformSize, 32u);
    for (const float sample : answer)
    {
        EXPECT_NEAR(sample, 0.25f, 1.0e-4f);
    }
}

TEST(PatchDenoiseTest, AGuidedShrinkageCoversAnInteriorNonFlatFrame)
{
    // This non-power-of-two extent makes the coverage pyramid place patches away from every edge.
    // A sanitizer run therefore checks the positive-origin weight indexing as well as the answer.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 47u;
    constexpr std::uint32_t kHeight = 43u;
    const std::vector<float> noisy = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.18f, 81u);
    std::vector<float> guide = noisy;
    for (std::uint32_t y = 1u; y + 1u < kHeight; ++y)
    {
        for (std::uint32_t x = 1u; x + 1u < kWidth; ++x)
        {
            double sum = 0.0;
            for (std::uint32_t dy = 0u; dy < 3u; ++dy)
            {
                for (std::uint32_t dx = 0u; dx < 3u; ++dx)
                {
                    sum += noisy[static_cast<std::size_t>(y + dy - 1u) * kWidth + x + dx - 1u];
                }
            }
            guide[static_cast<std::size_t>(y) * kWidth + x] = static_cast<float>(sum / 9.0);
        }
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::GuidedShrinkageRequest request;
    request.sigma = 0.04;
    request.radius = 4u;
    request.coverageThreshold = 3.0;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());

    std::vector<float> answer(noisy.size(), -1.0f);
    const an::Result<an::GuidedShrinkageResult> run = an::shrinkImageWithGuide(
        execution, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_GT(run->patchesPlaced, 1u);
    EXPECT_GT(run->samplesChanged, 0u);
    for (const float sample : answer)
    {
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_GE(sample, 0.0f);
        EXPECT_LE(sample, 1.0f);
    }
}

TEST(PatchDenoiseTest, TheCompiledGuidedExponentialAgreesWithTheScalarReference)
{
    // The guided stage evaluates two bilateral kernels and one spectral attenuation for every
    // patch. Its compiled exponential is the throughput path, while a forced reference execution
    // retains the scalar library as the numeric authority.
    const an::EngineContext engine = makeEngine(8u);
    const an::ExecutionContext compiled = makeExecution(engine, 3u, an::ComputeBackend::Cpu);
    const an::ExecutionContext reference = makeExecution(engine, 3u, an::ComputeBackend::Reference);
    constexpr std::uint32_t kWidth = 47u;
    constexpr std::uint32_t kHeight = 43u;
    const std::vector<float> noisy = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.18f, 91u);
    const std::vector<float> guide = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.06f, 93u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::GuidedShrinkageRequest request;
    request.sigma = 0.04;
    request.radius = 4u;
    request.coverageThreshold = 3.0;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());

    std::vector<float> compiledAnswer(noisy.size(), 0.0f);
    std::vector<float> referenceAnswer(noisy.size(), 0.0f);
    const an::Result<an::GuidedShrinkageResult> compiledRun =
        an::shrinkImageWithGuide(compiled, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout,
                                 compiledAnswer.data(), layout, *plan);
    const an::Result<an::GuidedShrinkageResult> referenceRun =
        an::shrinkImageWithGuide(reference, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout,
                                 referenceAnswer.data(), layout, *plan);
    ASSERT_TRUE(compiledRun.hasValue()) << compiledRun.status().message();
    ASSERT_TRUE(referenceRun.hasValue()) << referenceRun.status().message();
    EXPECT_EQ(compiledRun->patchesPlaced, referenceRun->patchesPlaced);
    for (std::size_t index = 0u; index < compiledAnswer.size(); ++index)
    {
        EXPECT_NEAR(compiledAnswer[index], referenceAnswer[index], 2.0e-5f) << "sample " << index;
    }
}

TEST(PatchDenoiseTest, AMeasuredNoisePastTheEstimatorCeilingIsRefusedBeforeNarrowing)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 65u;
    constexpr std::uint32_t kHeight = 32u;
    std::vector<float> source(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        source[index] = index % 2u == 0u ? 0.0f : 1000.0f;
    }
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);
    an::PatchDenoiseRequest request = plainRequest(0.02, 1.0);
    request.measureNoise = true;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());

    std::vector<float> answer(source.size(), -1.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_FALSE(run.hasValue());
    EXPECT_EQ(run.status().code(), an::ErrorCode::OutOfRange);
    EXPECT_TRUE(std::all_of(answer.begin(), answer.end(), [](float sample) { return sample == -1.0f; }));
}

TEST(PatchDenoiseTest, SharedCouplingWritesOneAnswerToEveryChannel)
{
    // This is not decided by comparing a colour frame's channels against each other to within a
    // millionth. It is a field on the request, so a caller who knows the noise is monochromatic says
    // so.
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 64u;
    constexpr std::uint32_t kHeight = 64u;
    const std::vector<float> noisy = noisyFrame(kWidth, kHeight, 3u, 0.5f, 0.1f, 21u);
    const std::vector<float> guide = constantFrame(kWidth, kHeight, 3u, 0.5f);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 3u);

    an::GuidedShrinkageRequest request;
    request.sigma = 0.03;
    request.radius = 8u;
    request.channelCoupling = an::GuidedShrinkageChannelCoupling::Shared;
    const an::Result<an::GuidedShrinkagePlan> plan = an::planGuidedShrinkage(request);
    ASSERT_TRUE(plan.hasValue());
    std::vector<float> answer(noisy.size(), 0.0f);
    ASSERT_TRUE(an::shrinkImageWithGuide(execution, an::SampleType::Float32, noisy.data(), layout, guide.data(), layout,
                                         answer.data(), layout, *plan)
                    .hasValue());
    const std::size_t plane = static_cast<std::size_t>(kWidth) * kHeight;
    for (std::size_t i = 0u; i < plane; ++i)
    {
        EXPECT_FLOAT_EQ(answer[i], answer[plane + i]);
        EXPECT_FLOAT_EQ(answer[i], answer[2u * plane + i]);
    }
}

TEST(PatchDenoiseTest, AFinishingStageRunsAndSaysThatItDid)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 128u;
    const std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.2f, 13u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::PatchDenoiseRequest request = plainRequest(0.05, 1.0);
    request.finishing = an::PatchDenoiseFinishing::GuidedDualDomain;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(plan->imagePasses(), 3u);

    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_TRUE(run->finishingRan);
    EXPECT_GT(run->finishingPatches, 0u);
    EXPECT_LT(run->outputNoise, run->inputNoise);
}

TEST(PatchDenoiseTest, TheStrengtheningLoopRunsItsIterationsAndReportsThem)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    constexpr std::uint32_t kWidth = 128u;
    constexpr std::uint32_t kHeight = 128u;
    const std::vector<float> source = noisyFrame(kWidth, kHeight, 1u, 0.5f, 0.2f, 17u);
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight);

    an::PatchDenoiseRequest request = plainRequest(0.05, 1.0);
    request.iterations = 2u;
    request.reinjection = 0.5;
    const an::Result<an::PatchDenoisePlan> plan = an::planPatchDenoise(request, 1u);
    ASSERT_TRUE(plan.hasValue());
    EXPECT_EQ(plan->imagePasses(), 4u);

    std::vector<float> answer(source.size(), 0.0f);
    const an::Result<an::PatchDenoiseResult> run = an::denoiseImagePatches(
        execution, an::SampleType::Float32, source.data(), layout, answer.data(), layout, *plan);
    ASSERT_TRUE(run.hasValue()) << run.status().message();
    EXPECT_GE(run->iterationsRun, 1u);
    EXPECT_LE(run->iterationsRun, 2u);
}

} // namespace

TEST(PatchDenoiseTest, RejectedSamplesDoNotInventNoiseAtTheirBoundaries)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    constexpr std::uint32_t width = 65u;
    constexpr std::uint32_t height = 8u;
    const auto layout = planarLayout(an::SampleType::Float32, width, height);
    std::vector<float> samples(width * height, 0.5f);
    for (std::uint32_t y = 0u; y < height; ++y)
        for (std::uint32_t x = 8u; x < 24u; ++x)
            samples[y * width + x] = 0.0f;
    const auto validity = an::ValidityDescriptor::create({.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(validity.has_value());
    an::SpatialSelection selection;
    selection.validity = *validity;
    const auto measured = an::estimateImageNoise(execution, an::SampleType::Float32, samples.data(), layout, selection);
    ASSERT_TRUE(measured.hasValue());
    EXPECT_DOUBLE_EQ(*measured, 0.0);
}
