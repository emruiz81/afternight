// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Conformance for the demosaic family: real SIMD differentials, truthful scalar fallbacks, and
// phase-sensitive properties for tabulated mosaics.
//
// This is the conformance row of the demosaic family, and it asserts an equality rather than a
// tolerance. That is a deliberate contract and not an accident of the current build: a vector kernel
// in this family is written with the same associations as its scalar reference, contraction is off
// in every translation unit of this engine, and the weights are exact divisions - so the compiled
// body produces the same bits, and a method whose compiled body stops doing so has to say why by
// changing this file.
//
// All seven estimates with compiled bodies are forced through every runnable demosaic ISA and
// compared bit for bit with the reference. The three scalar-first estimates are checked below for
// their own kernel identities, reaches and scratch. The tabulated methods separately run random
// pictures at all thirty-six origins, including the perceptual Markesteijn branch, so a missing
// compiled body cannot narrow silently to the scalar implementation and call that conformance.

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{

/// One real camera table, for the tabulated methods.
constexpr an::CfaColorEntries kCameraTable = {
    an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Green,
    an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green,
    an::CfaColor::Green, an::CfaColor::Red,   an::CfaColor::Green, an::CfaColor::Green, an::CfaColor::Blue,
    an::CfaColor::Red,   an::CfaColor::Blue,  an::CfaColor::Green, an::CfaColor::Blue,  an::CfaColor::Red,
    an::CfaColor::Green};

/// A deterministic pseudo-random sequence, so a differential sweep is reproducible.
class Sequence
{
public:
    /// Starts a sequence.
    ///
    /// @param seed where to start.
    explicit Sequence(std::uint64_t seed) noexcept : m_state(seed | 1u) {}

    /// The next value, uniform over the unit interval.
    ///
    /// @return the value.
    [[nodiscard]] float next() noexcept
    {
        m_state = m_state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>((m_state >> 40) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

private:
    /// The generator state.
    std::uint64_t m_state;
};

/// A planar layout with no row padding.
///
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param channels how many channels.
/// @return the layout.
[[nodiscard]] an::ImageLayout planeLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

/// An execution over one engine context, forced to one implementation.
///
/// @param engine the context to lease from.
/// @param backend which implementation to force.
/// @param instructionSet which CPU dialect to force.
/// @param fallback whether an absent implementation may narrow the request.
/// @return the execution.
[[nodiscard]] an::ExecutionContext forcedExecution(const an::EngineContext& engine, an::ComputeBackend backend,
                                                   an::InstructionSet instructionSet = an::InstructionSet::Automatic,
                                                   an::BackendFallback fallback = an::BackendFallback::Forbidden)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = backend;
    options.instructionSet = instructionSet;
    options.backendFallback = fallback;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// Every mosaic a method of this shape can be given.
///
/// @param tabulatedShape whether the method estimates a tabulated mosaic.
/// @return one descriptor per arrangement and phase.
[[nodiscard]] std::vector<an::CfaDescriptor> everyMosaic(bool tabulatedShape)
{
    std::vector<an::CfaDescriptor> mosaics;
    if (tabulatedShape)
    {
        for (std::uint32_t originY = 0u; originY < an::kCfaTableSide; ++originY)
        {
            for (std::uint32_t originX = 0u; originX < an::kCfaTableSide; ++originX)
            {
                const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(kCameraTable, originX, originY);
                EXPECT_TRUE(cfa.has_value());
                mosaics.push_back(cfa.value_or(an::CfaDescriptor{}));
            }
        }
        return mosaics;
    }
    for (const an::CfaPattern pattern :
         {an::CfaPattern::Rggb, an::CfaPattern::Bggr, an::CfaPattern::Grbg, an::CfaPattern::Gbrg})
    {
        for (std::uint32_t originY = 0u; originY < 2u; ++originY)
        {
            for (std::uint32_t originX = 0u; originX < 2u; ++originX)
            {
                const std::optional<an::CfaDescriptor> cfa = an::CfaDescriptor::create(pattern, originX, originY);
                EXPECT_TRUE(cfa.has_value());
                mosaics.push_back(cfa.value_or(an::CfaDescriptor{}));
            }
        }
    }
    return mosaics;
}

/// The vector dialects that contain demosaic kernels and this host can run.
///
/// @return every forceable dialect, in ascending order.
[[nodiscard]] std::vector<an::InstructionSet> runnableDemosaicTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (const an::InstructionSet candidate : {an::InstructionSet::Avx2, an::InstructionSet::Avx512Skylake})
    {
        if (an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

TEST(DemosaicConformanceTest, TheDispatchedImplementationMatchesItsReferenceBitForBit)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const an::ExecutionContext reference = forcedExecution(*engine, an::ComputeBackend::Reference);

    constexpr an::DemosaicMethod kMethods[] = {an::DemosaicMethod::Bilinear,
                                               an::DemosaicMethod::Fast,
                                               an::DemosaicMethod::Rcd,
                                               an::DemosaicMethod::Amaze,
                                               an::DemosaicMethod::XTransFast,
                                               an::DemosaicMethod::MarkesteijnOnePass,
                                               an::DemosaicMethod::MarkesteijnThreePass,
                                               an::DemosaicMethod::LuminanceAhd};
    // One picture wider than a tile of every method and one narrower, so both the whole-tile path and
    // the single-tile path are swept, and neither dimension is a multiple of a vector width.
    constexpr std::pair<std::uint32_t, std::uint32_t> kSizes[] = {{101u, 87u}, {263u, 71u}};

    const std::vector<an::InstructionSet> targets = runnableDemosaicTargets();
    ASSERT_FALSE(targets.empty());
    std::uint64_t seed = 11u;
    for (const an::InstructionSet target : targets)
    {
        const an::ExecutionContext compiled = forcedExecution(*engine, an::ComputeBackend::Cpu, target);
        for (const an::DemosaicMethod method : kMethods)
        {
            const bool tabulatedShape = !an::demosaicMethodAppliesTo(method, an::CfaPattern::Rggb);
            for (const an::CfaDescriptor& cfa : everyMosaic(tabulatedShape))
            {
                const an::Result<an::DemosaicPlan> plan = an::planDemosaic(an::DemosaicRequest{.method = method}, cfa);
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                for (const auto& size : kSizes)
                {
                    const std::uint32_t width = size.first;
                    const std::uint32_t height = size.second;
                    Sequence sequence{++seed};
                    std::vector<float> mosaic(static_cast<std::size_t>(width) * height, 0.0f);
                    for (float& sample : mosaic)
                    {
                        sample = sequence.next();
                    }
                    std::vector<float> scalar(mosaic.size() * 3u, -1.0f);
                    std::vector<float> dispatched(mosaic.size() * 3u, -2.0f);
                    const an::ImageLayout sourceLayout = planeLayout(width, height, 1u);
                    const an::ImageLayout colourLayout = planeLayout(width, height, 3u);
                    const an::Result<an::DemosaicResult> first =
                        an::demosaicImage(reference, an::SampleType::Float32, mosaic.data(), sourceLayout,
                                          scalar.data(), colourLayout, plan.value());
                    ASSERT_TRUE(first.hasValue()) << first.status().message();
                    ASSERT_EQ(first.value().backend.backend, an::ComputeBackend::Reference);
                    ASSERT_EQ(first.value().backend.instructionSet, an::InstructionSet::Scalar);
                    const an::Result<an::DemosaicResult> second =
                        an::demosaicImage(compiled, an::SampleType::Float32, mosaic.data(), sourceLayout,
                                          dispatched.data(), colourLayout, plan.value());
                    ASSERT_TRUE(second.hasValue()) << second.status().message();
                    ASSERT_EQ(second.value().backend.backend, an::ComputeBackend::Cpu);
                    ASSERT_EQ(second.value().backend.instructionSet, target);
                    for (std::size_t index = 0u; index < scalar.size(); ++index)
                    {
                        // Compared as bits, so that a zero of the other sign or a different not-a-number
                        // is a difference, as it is to anything that reads the stored samples.
                        ASSERT_EQ(std::bit_cast<std::uint32_t>(scalar[index]),
                                  std::bit_cast<std::uint32_t>(dispatched[index]))
                            << an::demosaicMethodName(method) << " with " << an::instructionSetName(target) << " on "
                            << an::cfaPatternName(cfa.pattern()) << "@" << cfa.originX() << "," << cfa.originY()
                            << " at " << width << "x" << height << " sample " << index;
                    }
                }
            }
        }
    }
}

TEST(DemosaicConformanceTest, TheCompiledLuminanceAhdKeepsTheReferencesBitsAtTheEdgesOfItsArithmetic)
{
    // The compiled passes order every comparison as `std::min` and `std::max` do, so a sample beyond the white level,
    // below zero, a zero of either sign, an infinity and a not-a-number - each of which an instruction's own minimum
    // or maximum would answer differently - come back as the reference writes them, at every phase of the filter.
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const an::ExecutionContext reference = forcedExecution(*engine, an::ComputeBackend::Reference);
    const std::vector<an::InstructionSet> targets = runnableDemosaicTargets();
    ASSERT_FALSE(targets.empty());
    constexpr std::uint32_t kWidth = 157u;
    constexpr std::uint32_t kHeight = 93u;
    const an::ImageLayout sourceLayout = planeLayout(kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planeLayout(kWidth, kHeight, 3u);
    std::uint64_t seed = 20261016u;
    for (const an::InstructionSet target : targets)
    {
        const an::ExecutionContext compiled = forcedExecution(*engine, an::ComputeBackend::Cpu, target);
        for (const an::CfaDescriptor& cfa : everyMosaic(false))
        {
            for (const double whiteLevel : {1.0, 0.75})
            {
                const an::Result<an::DemosaicPlan> plan =
                    an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::LuminanceAhd,
                                                         .domain = {.whiteLevel = whiteLevel}},
                                     cfa);
                ASSERT_TRUE(plan.hasValue()) << plan.status().message();
                Sequence sequence{++seed};
                std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
                for (float& sample : mosaic)
                {
                    // Mostly inside the range, some of it above the white level and some below zero.
                    sample = 1.3f * sequence.next() - 0.15f;
                }
                // Flat runs make the two candidates' counts tie, which takes their average.
                for (std::uint32_t x = 20u; x < 60u; ++x)
                {
                    mosaic[static_cast<std::size_t>(50u) * kWidth + x] = 0.5f;
                    mosaic[static_cast<std::size_t>(51u) * kWidth + x] = 0.5f;
                }
                mosaic[static_cast<std::size_t>(11u) * kWidth + 17u] = -0.0f;
                mosaic[static_cast<std::size_t>(12u) * kWidth + 18u] = 0.0f;
                mosaic[static_cast<std::size_t>(30u) * kWidth + 77u] = std::numeric_limits<float>::quiet_NaN();
                mosaic[static_cast<std::size_t>(70u) * kWidth + 120u] = std::numeric_limits<float>::infinity();
                mosaic[static_cast<std::size_t>(80u) * kWidth + 40u] = -std::numeric_limits<float>::infinity();
                std::vector<float> scalar(mosaic.size() * 3u, -1.0f);
                std::vector<float> dispatched(mosaic.size() * 3u, -2.0f);
                const an::Result<an::DemosaicResult> first =
                    an::demosaicImage(reference, an::SampleType::Float32, mosaic.data(), sourceLayout, scalar.data(),
                                      colourLayout, *plan);
                ASSERT_TRUE(first.hasValue()) << first.status().message();
                const an::Result<an::DemosaicResult> second =
                    an::demosaicImage(compiled, an::SampleType::Float32, mosaic.data(), sourceLayout, dispatched.data(),
                                      colourLayout, *plan);
                ASSERT_TRUE(second.hasValue()) << second.status().message();
                ASSERT_EQ(second.value().backend.backend, an::ComputeBackend::Cpu);
                ASSERT_EQ(second.value().backend.instructionSet, target);
                for (std::size_t index = 0u; index < scalar.size(); ++index)
                {
                    ASSERT_EQ(std::bit_cast<std::uint32_t>(scalar[index]),
                              std::bit_cast<std::uint32_t>(dispatched[index]))
                        << an::instructionSetName(target) << " on " << an::cfaPatternName(cfa.pattern()) << "@"
                        << cfa.originX() << "," << cfa.originY() << " white " << whiteLevel << " sample " << index;
                }
            }
        }
    }
}

TEST(DemosaicConformanceTest, EveryMethodSatisfiesARequiredCpuRequestWithoutFallingBack)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const std::vector<an::InstructionSet> targets = runnableDemosaicTargets();
    ASSERT_FALSE(targets.empty());
    const an::ExecutionContext required = forcedExecution(*engine, an::ComputeBackend::Cpu, targets.front());
    const an::ExecutionContext allowed =
        forcedExecution(*engine, an::ComputeBackend::Cpu, an::InstructionSet::Automatic, an::BackendFallback::Allowed);

    constexpr an::DemosaicMethod kMethods[] = {an::DemosaicMethod::Rcd,
                                               an::DemosaicMethod::Amaze,
                                               an::DemosaicMethod::XTransFast,
                                               an::DemosaicMethod::MarkesteijnOnePass,
                                               an::DemosaicMethod::MarkesteijnThreePass,
                                               an::DemosaicMethod::LuminanceAhd};
    constexpr std::uint32_t kWidth = 83u;
    constexpr std::uint32_t kHeight = 71u;
    Sequence sequence{91u};
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (float& sample : mosaic)
    {
        sample = sequence.next();
    }
    const an::ImageLayout sourceLayout = planeLayout(kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planeLayout(kWidth, kHeight, 3u);

    for (const an::DemosaicMethod method : kMethods)
    {
        const bool tabulatedShape = !an::demosaicMethodAppliesTo(method, an::CfaPattern::Rggb);
        const an::CfaDescriptor cfa = everyMosaic(tabulatedShape).front();
        const an::Result<an::DemosaicPlan> plan = an::planDemosaic(an::DemosaicRequest{.method = method}, cfa);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();

        std::vector<float> requiredOutput(mosaic.size() * 3u, -7.0f);
        const an::Result<an::DemosaicResult> first = an::demosaicImage(
            required, an::SampleType::Float32, mosaic.data(), sourceLayout, requiredOutput.data(), colourLayout, *plan);
        ASSERT_TRUE(first.hasValue()) << an::demosaicMethodName(method) << ": " << first.status().message();
        EXPECT_EQ(first->backend.backend, an::ComputeBackend::Cpu) << an::demosaicMethodName(method);
        EXPECT_EQ(first->backend.instructionSet, targets.front()) << an::demosaicMethodName(method);

        std::vector<float> fallback(mosaic.size() * 3u, -8.0f);
        const an::Result<an::DemosaicResult> second = an::demosaicImage(
            allowed, an::SampleType::Float32, mosaic.data(), sourceLayout, fallback.data(), colourLayout, *plan);
        ASSERT_TRUE(second.hasValue()) << an::demosaicMethodName(method) << ": " << second.status().message();
        EXPECT_EQ(second->backend.backend, an::ComputeBackend::Cpu) << an::demosaicMethodName(method);
        EXPECT_NE(second->backend.instructionSet, an::InstructionSet::Scalar) << an::demosaicMethodName(method);
    }
}

TEST(DemosaicConformanceTest, Avx512SkylakeExecutesEveryEstimateWhenBuildAndHostOfferIt)
{
    constexpr an::InstructionSet kTarget = an::InstructionSet::Avx512Skylake;
    if (!an::supportsInstructionSet(an::cpuCapabilities(), kTarget))
    {
        GTEST_SKIP() << "this physical host does not offer the AVX-512 Skylake subset";
    }
    if (!an::instructionSetIsCompiledIn(kTarget))
    {
        GTEST_SKIP() << "this compiler does not emit the AVX-512 target";
    }

    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 4u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const an::ExecutionContext execution = forcedExecution(*engine, an::ComputeBackend::Cpu, kTarget);

    constexpr an::DemosaicMethod kMethods[] = {an::DemosaicMethod::Bilinear,
                                               an::DemosaicMethod::Fast,
                                               an::DemosaicMethod::Rcd,
                                               an::DemosaicMethod::Amaze,
                                               an::DemosaicMethod::XTransFast,
                                               an::DemosaicMethod::MarkesteijnOnePass,
                                               an::DemosaicMethod::MarkesteijnThreePass};
    constexpr std::uint32_t kWidth = 263u;
    constexpr std::uint32_t kHeight = 71u;
    Sequence sequence{512u};
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (float& sample : mosaic)
    {
        sample = sequence.next();
    }
    const an::ImageLayout sourceLayout = planeLayout(kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planeLayout(kWidth, kHeight, 3u);

    for (const an::DemosaicMethod method : kMethods)
    {
        const bool tabulatedShape = !an::demosaicMethodAppliesTo(method, an::CfaPattern::Rggb);
        const an::CfaDescriptor cfa = everyMosaic(tabulatedShape).front();
        const an::Result<an::DemosaicPlan> plan = an::planDemosaic(an::DemosaicRequest{.method = method}, cfa);
        ASSERT_TRUE(plan.hasValue()) << an::demosaicMethodName(method) << ": " << plan.status().message();
        std::vector<float> output(mosaic.size() * 3u, -1.0f);
        const an::Result<an::DemosaicResult> ran = an::demosaicImage(execution, an::SampleType::Float32, mosaic.data(),
                                                                     sourceLayout, output.data(), colourLayout, *plan);
        ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method) << ": " << ran.status().message();
        EXPECT_EQ(ran->backend.backend, an::ComputeBackend::Cpu) << an::demosaicMethodName(method);
        EXPECT_EQ(ran->backend.instructionSet, kTarget) << an::demosaicMethodName(method);
    }
}

TEST(DemosaicConformanceTest, EveryTabulatedOriginRunsAgainstItsOwnFilterPhase)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 3u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const an::ExecutionContext reference = forcedExecution(*engine, an::ComputeBackend::Reference);

    const std::vector<an::CfaDescriptor> mosaics = everyMosaic(true);
    ASSERT_EQ(mosaics.size(), an::kCfaTableSide * an::kCfaTableSide);
    constexpr an::DemosaicMethod kMethods[] = {an::DemosaicMethod::XTransFast, an::DemosaicMethod::MarkesteijnOnePass,
                                               an::DemosaicMethod::MarkesteijnThreePass};
    constexpr std::uint32_t kWidth = 101u;
    constexpr std::uint32_t kHeight = 87u;
    const an::ImageLayout sourceLayout = planeLayout(kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planeLayout(kWidth, kHeight, 3u);
    std::uint64_t seed = 713u;

    for (const an::DemosaicMethod method : kMethods)
    {
        for (const an::CfaDescriptor& cfa : mosaics)
        {
            Sequence sequence{++seed};
            std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
            for (float& sample : mosaic)
            {
                sample = sequence.next();
            }
            const an::Result<an::DemosaicPlan> plan = an::planDemosaic(an::DemosaicRequest{.method = method}, cfa);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            std::vector<float> destination(mosaic.size() * 3u, -1.0f);
            const an::Result<an::DemosaicResult> ran =
                an::demosaicImage(reference, an::SampleType::Float32, mosaic.data(), sourceLayout, destination.data(),
                                  colourLayout, *plan);
            ASSERT_TRUE(ran.hasValue()) << an::demosaicMethodName(method) << " @" << cfa.originX() << ","
                                        << cfa.originY() << ": " << ran.status().message();
            EXPECT_TRUE(
                std::all_of(destination.begin(), destination.end(), [](float sample) { return std::isfinite(sample); }))
                << an::demosaicMethodName(method) << " @" << cfa.originX() << "," << cfa.originY();

            if (method == an::DemosaicMethod::XTransFast)
            {
                const std::size_t planeSamples = mosaic.size();
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < kWidth; ++x)
                    {
                        const std::size_t index = static_cast<std::size_t>(y) * kWidth + x;
                        const std::size_t channel = static_cast<std::size_t>(cfa.colorAt(x, y));
                        ASSERT_NEAR(destination[channel * planeSamples + index], mosaic[index], 2e-6f)
                            << "phase " << cfa.originX() << "," << cfa.originY() << " at " << x << "," << y;
                    }
                }
            }
        }
    }
}

TEST(DemosaicConformanceTest, MarkesteijnExercisesItsPerceptualDirectionChoice)
{
    an::EngineContextOptions engineOptions;
    engineOptions.maxConcurrency = 3u;
    an::Result<an::EngineContext> engine = an::EngineContext::create(engineOptions);
    ASSERT_TRUE(engine.hasValue()) << engine.status().message();
    const an::ExecutionContext reference = forcedExecution(*engine, an::ComputeBackend::Reference);

    constexpr std::uint32_t kWidth = 127u;
    constexpr std::uint32_t kHeight = 103u;
    const an::CfaDescriptor cfa = everyMosaic(true).back();
    Sequence sequence{1907u};
    std::vector<float> mosaic(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (float& sample : mosaic)
    {
        sample = sequence.next();
    }

    const an::DemosaicRequest ordinaryRequest{.method = an::DemosaicMethod::MarkesteijnThreePass};
    an::DemosaicRequest perceptualRequest = ordinaryRequest;
    perceptualRequest.useCieLab = true;
    const an::Result<an::DemosaicPlan> ordinaryPlan = an::planDemosaic(ordinaryRequest, cfa);
    const an::Result<an::DemosaicPlan> perceptualPlan = an::planDemosaic(perceptualRequest, cfa);
    ASSERT_TRUE(ordinaryPlan.hasValue() && perceptualPlan.hasValue());

    std::vector<float> ordinary(mosaic.size() * 3u, -1.0f);
    std::vector<float> perceptual(mosaic.size() * 3u, -2.0f);
    const an::ImageLayout sourceLayout = planeLayout(kWidth, kHeight, 1u);
    const an::ImageLayout colourLayout = planeLayout(kWidth, kHeight, 3u);
    const an::Result<an::DemosaicResult> first = an::demosaicImage(
        reference, an::SampleType::Float32, mosaic.data(), sourceLayout, ordinary.data(), colourLayout, *ordinaryPlan);
    const an::Result<an::DemosaicResult> second =
        an::demosaicImage(reference, an::SampleType::Float32, mosaic.data(), sourceLayout, perceptual.data(),
                          colourLayout, *perceptualPlan);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_TRUE(std::all_of(perceptual.begin(), perceptual.end(), [](float sample) { return std::isfinite(sample); }));
    EXPECT_FALSE(std::equal(ordinary.begin(), ordinary.end(), perceptual.begin()));
}

TEST(DemosaicConformanceTest, EachEstimateReportsTheKernelContractItRuns)
{
    constexpr an::DemosaicMethod kBayerMethods[] = {an::DemosaicMethod::Bilinear,    an::DemosaicMethod::Fast,
                                                    an::DemosaicMethod::Rcd,         an::DemosaicMethod::Amaze,
                                                    an::DemosaicMethod::Lmmse,       an::DemosaicMethod::Vng4,
                                                    an::DemosaicMethod::LuminanceAhd};
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    for (const an::DemosaicMethod method : kBayerMethods)
    {
        const an::Result<an::DemosaicPlan> plan = an::planDemosaic(an::DemosaicRequest{.method = method}, *rggb);
        ASSERT_TRUE(plan.hasValue());
        const an::KernelId id = plan.value().kernelId();
        EXPECT_TRUE(an::isKernelId(id)) << an::demosaicMethodName(method);
        EXPECT_EQ(an::kernelFamilyOf(id.operation), an::KernelFamily::Stencil) << an::demosaicMethodName(method);
        EXPECT_EQ(plan.value().reach(), an::demosaicMethodReach(method));
        // The cheapest estimate needs no working plane at all, and saying so is part of the
        // contract: an operation that reserved scratch it never used would charge the engine
        // context for memory no kernel touches.
        const std::uint64_t scratch = plan.value().tileScratchBytes(64u, 64u);
        EXPECT_EQ(scratch == 0u, method == an::DemosaicMethod::Bilinear) << an::demosaicMethodName(method);
    }
    const std::optional<an::CfaDescriptor> table = an::CfaDescriptor::create(kCameraTable, 0u, 0u);
    ASSERT_TRUE(table.has_value());
    const an::Result<an::DemosaicPlan> onePass =
        an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::MarkesteijnOnePass}, *table);
    const an::Result<an::DemosaicPlan> threePass =
        an::planDemosaic(an::DemosaicRequest{.method = an::DemosaicMethod::MarkesteijnThreePass}, *table);
    ASSERT_TRUE(onePass.hasValue() && threePass.hasValue());
    // The two passes are revisions of one numeric contract, and the three-pass estimate holds more.
    EXPECT_EQ(onePass.value().kernelId(), threePass.value().kernelId());
    EXPECT_LT(onePass.value().tileScratchBytes(64u, 64u), threePass.value().tileScratchBytes(64u, 64u));
}

} // namespace
