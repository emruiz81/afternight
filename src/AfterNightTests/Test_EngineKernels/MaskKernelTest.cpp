// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Differential tests for the vectorized packed-mask paths against their scalar reference.

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeReferenceExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = an::ComputeBackend::Reference;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ExecutionContext makeVectorExecution(const an::EngineContext& engine,
                                                       an::InstructionSet instructionSet)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 2u;
    options.backend = an::ComputeBackend::Cpu;
    options.instructionSet = instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] std::vector<an::InstructionSet> runnableVectorTargets()
{
    std::vector<an::InstructionSet> targets;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate != an::InstructionSet::Automatic && candidate != an::InstructionSet::Scalar &&
            an::instructionSetIsCompiledIn(candidate) && an::supportsInstructionSet(capabilities, candidate))
        {
            targets.push_back(candidate);
        }
    }
    return targets;
}

/// A packed mask over caller-owned bytes.
struct OwnedMask
{
    /// The bytes the mask lives in.
    std::vector<std::uint8_t> bytes;

    /// The mask's dimensions.
    an::ImageExtent extent;
};

[[nodiscard]] an::ImageExtent maskExtent(std::uint32_t width, std::uint32_t height, std::uint32_t planes)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, planes, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return *extent;
}

/// A mask whose bytes follow a fixed pseudo-random pattern, with the pad bits of every row cleared.
[[nodiscard]] OwnedMask patternedMask(std::uint32_t width, std::uint32_t height, std::uint32_t planes,
                                      std::uint32_t seed)
{
    const an::ImageExtent extent = maskExtent(width, height, planes);
    const std::size_t rowBytes = static_cast<std::size_t>(an::packedMaskRowBytes(width));
    std::vector<std::uint8_t> bytes(rowBytes * height * planes, 0u);
    std::uint32_t state = seed | 1u;
    for (std::uint8_t& byte : bytes)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24u);
    }
    OwnedMask mask{std::move(bytes), extent};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>(mask.bytes), mask.extent);
    EXPECT_TRUE(view.has_value());
    view->clearPadBits();
    return mask;
}

[[nodiscard]] an::ValidityMaskView<std::uint8_t> viewOf(OwnedMask& mask)
{
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>(mask.bytes), mask.extent);
    EXPECT_TRUE(view.has_value());
    return *view;
}

[[nodiscard]] an::ValidityMaskView<const std::uint8_t> constViewOf(const OwnedMask& mask)
{
    const std::optional<an::ValidityMaskView<const std::uint8_t>> view =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>(mask.bytes), mask.extent);
    EXPECT_TRUE(view.has_value());
    return *view;
}

/// The widths that exercise the vector body, its tail, and a mask smaller than one vector.
constexpr std::array<std::uint32_t, 7u> kWidths{1u, 7u, 8u, 63u, 129u, 257u, 1023u};

} // namespace

TEST(MaskKernelTest, EveryRunnableTargetCombinesInvertsAndCountsExactlyAsTheReferenceDoes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext reference = makeReferenceExecution(engine);
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();

    std::uint32_t comparisons = 0u;
    for (const an::InstructionSet target : targets)
    {
        const an::ExecutionContext vector = makeVectorExecution(engine, target);
        const an::Result<an::BackendSelection> selected = an::selectBackend(vector);
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        ASSERT_EQ(selected->instructionSet, target);
        ASSERT_EQ(an::instructionSetOfDispatchedCode(vector), target);

        for (const std::uint32_t width : kWidths)
        {
            for (const std::uint32_t planes : {1u, 3u})
            {
                const std::uint32_t height = width <= 8u ? 5u : 2u;
                const OwnedMask left = patternedMask(width, height, planes, width * 7919u + planes);
                const OwnedMask right = patternedMask(width, height, planes, width * 104729u + planes * 31u);

                for (const an::ValidityCombination combination :
                     {an::ValidityCombination::Intersection, an::ValidityCombination::Union})
                {
                    OwnedMask referenceAnswer = patternedMask(width, height, planes, 12345u);
                    OwnedMask vectorAnswer = referenceAnswer;
                    ASSERT_TRUE(an::combineImageMasks(reference, viewOf(referenceAnswer), constViewOf(left),
                                                      constViewOf(right), combination)
                                    .ok());
                    ASSERT_TRUE(an::combineImageMasks(vector, viewOf(vectorAnswer), constViewOf(left),
                                                      constViewOf(right), combination)
                                    .ok());
                    EXPECT_EQ(referenceAnswer.bytes, vectorAnswer.bytes) << "width " << width << " planes " << planes;
                    ++comparisons;
                }

                OwnedMask referenceInverted = patternedMask(width, height, planes, 999u);
                OwnedMask vectorInverted = referenceInverted;
                ASSERT_TRUE(an::invertImageMask(reference, viewOf(referenceInverted), constViewOf(left)).ok());
                ASSERT_TRUE(an::invertImageMask(vector, viewOf(vectorInverted), constViewOf(left)).ok());
                EXPECT_EQ(referenceInverted.bytes, vectorInverted.bytes) << "width " << width;
                ++comparisons;

                const an::Result<an::ValidityCount> referenceCount = an::countImageMask(reference, constViewOf(left));
                const an::Result<an::ValidityCount> vectorCount = an::countImageMask(vector, constViewOf(left));
                ASSERT_TRUE(referenceCount.hasValue()) << referenceCount.status().message();
                ASSERT_TRUE(vectorCount.hasValue()) << vectorCount.status().message();
                EXPECT_EQ(referenceCount->valid, vectorCount->valid) << "width " << width;
                EXPECT_EQ(referenceCount->valid, constViewOf(left).countValid());
                EXPECT_EQ(vectorCount->total, static_cast<std::uint64_t>(width) * height * planes);
                ++comparisons;
            }
        }
    }

    // Nothing is claimed when this build or this host has no vector table; the reference path is
    // still the one every other case above compared against.
    if (targets.empty())
    {
        GTEST_SKIP() << "this build contains no vector kernels this host can run";
    }
    EXPECT_GT(comparisons, 0u);
}

TEST(MaskKernelTest, AMaskThatIsNotAWholeNumberOfVectorsStillLeavesItsPadBitsAtZero)
{
    const an::EngineContext engine = makeEngine();
    const std::vector<an::InstructionSet> targets = runnableVectorTargets();
    if (targets.empty())
    {
        GTEST_SKIP() << "this build contains no vector kernels this host can run";
    }

    for (const an::InstructionSet target : targets)
    {
        const an::ExecutionContext vector = makeVectorExecution(engine, target);
        for (const std::uint32_t width : kWidths)
        {
            if (width % 8u == 0u)
            {
                continue;
            }
            const std::uint8_t spare = static_cast<std::uint8_t>(~an::packedMaskLastByteMask(width));
            OwnedMask source = patternedMask(width, 3u, 2u, width + 17u);
            OwnedMask destination = patternedMask(width, 3u, 2u, width + 4242u);

            ASSERT_TRUE(an::invertImageMask(vector, viewOf(destination), constViewOf(source)).ok());
            const an::ValidityMaskView<const std::uint8_t> view = constViewOf(destination);
            for (std::uint32_t plane = 0u; plane < 2u; ++plane)
            {
                for (std::uint32_t y = 0u; y < 3u; ++y)
                {
                    EXPECT_EQ((*view.row(plane, y)).back() & spare, 0u) << "width " << width;
                }
            }

            // The count therefore agrees with the model's own walk of the same bits.
            const an::Result<an::ValidityCount> counted = an::countImageMask(vector, view);
            ASSERT_TRUE(counted.hasValue()) << counted.status().message();
            EXPECT_EQ(counted->valid, view.countValid()) << "width " << width;
        }
    }
}
