// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Differential tests for the transfer/layout kernel family across every compiled backend.
//
// The scalar oracle next door is only worth having if something is actually compared against it,
// and this is that comparison. Every case here runs one operation twice - once forced onto
// `ComputeBackend::Reference`, which is the `std::memcpy` oracle, and once forced onto each
// instruction set this build compiled kernels for - and requires the two results to be equal byte
// for byte over the whole destination buffer, padding included.
//
// Three properties are what the sweep is shaped around.
//
// **Bit preservation, not numeric agreement.** None of these operations computes anything, so
// "close enough" is not a tolerance this family has. The fixtures are pseudo-random *bytes* rather
// than plausible pixels, which means every sweep includes signalling not-a-number patterns,
// negative zero and denormals in the floating representations - the values an implementation that
// moved samples through a floating register would be free to change, and the reason the kernels use
// integer lanes.
//
// **Tails.** A vector kernel is right in its body long before it is right in the few samples at the
// end of a run that do not fill a register, and the widths below deliberately cluster around one,
// two and four times the widest vector this engine compiles for, in every element width.
//
// **Which backend actually ran.** Every case asserts the dispatched instruction set that the
// engine reports, so a sweep that silently ran the reference twice fails instead of passing
// trivially. That is the failure mode a differential harness is most likely to have.

#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageTransfer.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// One forced implementation the sweep compares against the oracle.
struct ForcedBackend
{
    /// Which family of implementations.
    an::ComputeBackend backend = an::ComputeBackend::Reference;

    /// Which compiled instruction set inside it.
    an::InstructionSet instructionSet = an::InstructionSet::Scalar;

    /// What to print when a case fails.
    std::string name;
};

/// An engine context, which the case has already failed if it cannot build.
///
/// @return the context.
[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 8u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// An operation pinned to one implementation, refusing every fallback.
///
/// Refusing fallback is what makes the sweep a comparison: an operation allowed to narrow its
/// request would run the same code twice and agree with itself.
///
/// @param engine the context to start it on.
/// @param forced which implementation.
/// @return the operation, or an empty one when the request was refused.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, const ForcedBackend& forced)
{
    an::ExecutionOptions options;
    // Two threads of the context's eight, so the oracle and every optimized backend hold an
    // operation at the same time without exhausting the budget, and every one of them still
    // partitions its rows rather than running the single-threaded path.
    options.maxConcurrency = 2u;
    options.backend = forced.backend;
    options.instructionSet = forced.instructionSet;
    options.backendFallback = an::BackendFallback::Forbidden;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << forced.name << ": " << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// Every optimized implementation this build compiled and this host can run.
///
/// The reference is deliberately not in the list: it is the other side of every comparison rather
/// than one of the things being compared. An empty list is a legitimate result on a host that can
/// run none of them, and the cases below say so rather than passing quietly.
///
/// @return the forced backends, in ladder order.
[[nodiscard]] std::vector<ForcedBackend> compiledBackends()
{
    std::vector<ForcedBackend> backends;
    const an::CpuCapabilities capabilities = an::cpuCapabilities();
    for (std::size_t index = 0u; index < an::kInstructionSetCount; ++index)
    {
        const an::InstructionSet candidate = static_cast<an::InstructionSet>(index);
        if (candidate == an::InstructionSet::Automatic || candidate == an::InstructionSet::Scalar)
        {
            continue;
        }
        if (!an::instructionSetIsCompiledIn(candidate) || !an::supportsInstructionSet(capabilities, candidate))
        {
            continue;
        }
        backends.push_back(ForcedBackend{.backend = an::ComputeBackend::Cpu,
                                         .instructionSet = candidate,
                                         .name = an::instructionSetName(candidate)});
    }
    return backends;
}

/// The oracle every case compares against.
///
/// @return the reference backend.
[[nodiscard]] ForcedBackend referenceBackend()
{
    return ForcedBackend{
        .backend = an::ComputeBackend::Reference, .instructionSet = an::InstructionSet::Scalar, .name = "Reference"};
}

/// An extent, which the case has already failed if it cannot build.
///
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @return the extent.
[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const std::optional<an::ImageExtent> made =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A layout of one representation, in a stated arrangement and with a stated row padding.
///
/// @param sampleType the representation, which is where the element size comes from.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @return the layout.
[[nodiscard]] an::ImageLayout layoutOf(an::SampleType sampleType, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t channelCount, bool interleaved,
                                       std::uint32_t rowPaddingSamples = 0u)
{
    const std::uint32_t elementSize = an::sampleTypeSizeBytes(sampleType);
    const std::uint32_t samplesPerPixel = interleaved ? channelCount : 1u;
    const std::uint32_t planeCount = interleaved ? 1u : channelCount;
    const an::ImageLayoutRequest request{.extent = extentOf(width, height, channelCount),
                                         .elementSizeBytes = elementSize,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(width) * samplesPerPixel + rowPaddingSamples) * elementSize;
    const std::uint64_t planeStride = rowStride * height;
    const std::optional<an::ImageLayout> made =
        an::ImageLayout::createWithStrides(request, rowStride, planeStride, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A buffer of pseudo-random bytes, sized for a layout.
///
/// Bytes rather than samples on purpose: this family moves object representations, so the fixture
/// that exercises it hardest is one that contains every bit pattern rather than one that contains
/// plausible pixels. In the floating representations that includes signalling not-a-number,
/// negative zero and denormals, which is exactly what a kernel routing samples through a floating
/// register would be free to alter.
///
/// @param byteCount how many bytes.
/// @param seed which sequence, so a source and a sentinel differ.
/// @return the buffer.
[[nodiscard]] std::vector<std::uint8_t> randomBytes(std::uint64_t byteCount, std::uint64_t seed)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byteCount), 0u);
    std::uint64_t state = seed * 6364136223846793005ull + 1442695040888963407ull;
    for (std::size_t index = 0u; index < bytes.size(); ++index)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        bytes[index] = static_cast<std::uint8_t>(state >> 33u);
    }
    return bytes;
}

/// Which representations the sweep covers.
///
/// Every one of them: the four element widths this family has bodies for, and both signednesses of
/// each, so a table that mapped a representation onto the wrong width would be caught rather than
/// only the widths themselves being exercised.
///
/// @return the eight representations.
[[nodiscard]] std::vector<an::SampleType> everyRepresentation()
{
    std::vector<an::SampleType> types;
    for (std::size_t index = 0u; index < an::kSampleTypeCount; ++index)
    {
        types.push_back(static_cast<an::SampleType>(index));
    }
    return types;
}

/// The widths one sweep walks.
///
/// Clustered around one, two and four times the widest vector this engine compiles for, in every
/// element width, because that is where a tail is either handled or lost.
///
/// @return the widths in pixels.
[[nodiscard]] std::vector<std::uint32_t> sweepWidths()
{
    return {1u, 2u, 3u, 5u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 129u};
}

/// Asserts that an operation dispatched to the implementation it was forced onto.
///
/// The check that keeps the sweep from passing by running the oracle twice.
///
/// @param execution the operation.
/// @param forced what it was pinned to.
void expectDispatchedTo(const an::ExecutionContext& execution, const ForcedBackend& forced)
{
    const an::Result<an::BackendSelection> selected = an::selectBackend(execution);
    ASSERT_TRUE(selected.hasValue()) << forced.name;
    EXPECT_EQ(selected->backend, forced.backend) << forced.name;
    EXPECT_EQ(selected->instructionSet, forced.instructionSet) << forced.name;
    EXPECT_EQ(an::instructionSetOfDispatchedCode(execution), forced.instructionSet)
        << forced.name << ": the compiled code reports "
        << an::instructionSetName(an::instructionSetOfDispatchedCode(execution));
}

} // namespace

TEST(LayoutBackendTest, EveryCompiledBackendCopiesExactlyWhatTheOracleCopies)
{
    an::EngineContext engine = makeEngine();
    const std::vector<ForcedBackend> optimized = compiledBackends();
    if (optimized.empty())
    {
        GTEST_SKIP() << "this build compiled no optimized kernels this host can run";
    }

    for (const an::SampleType sampleType : everyRepresentation())
    {
        for (const std::uint32_t width : sweepWidths())
        {
            for (const std::uint32_t padding : {0u, 3u})
            {
                const an::ImageLayout source = layoutOf(sampleType, width, 3u, 2u, false, padding);
                const an::ImageLayout destination = layoutOf(sampleType, width, 3u, 2u, false, padding == 0u ? 5u : 0u);
                const std::vector<std::uint8_t> input = randomBytes(source.totalBytes(), width + 1u);

                std::vector<std::uint8_t> expected = randomBytes(destination.totalBytes(), 99u);
                an::ExecutionContext oracle = makeExecution(engine, referenceBackend());
                ASSERT_TRUE(
                    an::copyImageSamples(oracle, sampleType, input.data(), source, expected.data(), destination).ok());

                for (const ForcedBackend& forced : optimized)
                {
                    std::vector<std::uint8_t> actual = randomBytes(destination.totalBytes(), 99u);
                    an::ExecutionContext execution = makeExecution(engine, forced);
                    expectDispatchedTo(execution, forced);
                    ASSERT_TRUE(
                        an::copyImageSamples(execution, sampleType, input.data(), source, actual.data(), destination)
                            .ok());
                    EXPECT_EQ(actual, expected)
                        << forced.name << " disagreed with the oracle on " << an::sampleTypeName(sampleType)
                        << " width " << width << " padding " << padding;
                }
            }
        }
    }
}

TEST(LayoutBackendTest, EveryCompiledBackendFillsExactlyWhatTheOracleFills)
{
    an::EngineContext engine = makeEngine();
    const std::vector<ForcedBackend> optimized = compiledBackends();
    if (optimized.empty())
    {
        GTEST_SKIP() << "this build compiled no optimized kernels this host can run";
    }

    for (const an::SampleType sampleType : everyRepresentation())
    {
        const std::vector<std::uint8_t> value = randomBytes(an::sampleTypeSizeBytes(sampleType), 7u);
        for (const std::uint32_t width : sweepWidths())
        {
            const an::ImageLayout layout = layoutOf(sampleType, width, 3u, 2u, false, 4u);

            std::vector<std::uint8_t> expected = randomBytes(layout.totalBytes(), 41u);
            an::ExecutionContext oracle = makeExecution(engine, referenceBackend());
            ASSERT_TRUE(an::fillImageSamples(oracle, sampleType, value.data(), expected.data(), layout).ok());

            for (const ForcedBackend& forced : optimized)
            {
                std::vector<std::uint8_t> actual = randomBytes(layout.totalBytes(), 41u);
                an::ExecutionContext execution = makeExecution(engine, forced);
                expectDispatchedTo(execution, forced);
                ASSERT_TRUE(an::fillImageSamples(execution, sampleType, value.data(), actual.data(), layout).ok());
                EXPECT_EQ(actual, expected) << forced.name << " disagreed with the oracle on "
                                            << an::sampleTypeName(sampleType) << " width " << width;
            }
        }
    }
}

TEST(LayoutBackendTest, EveryCompiledBackendRepacksExactlyWhatTheOracleRepacks)
{
    an::EngineContext engine = makeEngine();
    const std::vector<ForcedBackend> optimized = compiledBackends();
    if (optimized.empty())
    {
        GTEST_SKIP() << "this build compiled no optimized kernels this host can run";
    }

    // One channel and five channels bracket the whole-pixel kernels deliberately: below the range
    // the two arrangements are the same shape, above it the operation falls back to moving one
    // channel at a time, and both fallbacks have to produce the oracle's answer as exactly as the
    // vectorized middle does.
    for (const an::SampleType sampleType : everyRepresentation())
    {
        for (const std::uint32_t channelCount : {1u, 2u, 3u, 4u, 5u})
        {
            for (const std::uint32_t width : {1u, 3u, 8u, 17u, 33u, 65u})
            {
                for (const bool interleaveIt : {false, true})
                {
                    const an::ImageLayout source = layoutOf(sampleType, width, 3u, channelCount, !interleaveIt, 2u);
                    const an::ImageLayout destination = layoutOf(sampleType, width, 3u, channelCount, interleaveIt, 0u);
                    const std::vector<std::uint8_t> input = randomBytes(source.totalBytes(), width + channelCount);

                    std::vector<std::uint8_t> expected = randomBytes(destination.totalBytes(), 13u);
                    an::ExecutionContext oracle = makeExecution(engine, referenceBackend());
                    ASSERT_TRUE(
                        an::repackImageSamples(oracle, sampleType, input.data(), source, expected.data(), destination)
                            .ok());

                    for (const ForcedBackend& forced : optimized)
                    {
                        std::vector<std::uint8_t> actual = randomBytes(destination.totalBytes(), 13u);
                        an::ExecutionContext execution = makeExecution(engine, forced);
                        expectDispatchedTo(execution, forced);
                        ASSERT_TRUE(an::repackImageSamples(execution, sampleType, input.data(), source, actual.data(),
                                                           destination)
                                        .ok());
                        EXPECT_EQ(actual, expected) << forced.name << " disagreed with the oracle on "
                                                    << an::sampleTypeName(sampleType) << " channels " << channelCount
                                                    << " width " << width << (interleaveIt ? " pack" : " unpack");
                    }
                }
            }
        }
    }
}

TEST(LayoutBackendTest, EveryCompiledBackendReversesBytesExactlyAsTheOracleDoes)
{
    an::EngineContext engine = makeEngine();
    const std::vector<ForcedBackend> optimized = compiledBackends();
    if (optimized.empty())
    {
        GTEST_SKIP() << "this build compiled no optimized kernels this host can run";
    }

    for (const an::SampleType sampleType : everyRepresentation())
    {
        if (!an::isByteOrderMeaningful(sampleType))
        {
            continue;
        }
        for (const std::uint32_t width : sweepWidths())
        {
            const an::ImageLayout layout = layoutOf(sampleType, width, 3u, 2u, false, 1u);
            const std::vector<std::uint8_t> input = randomBytes(layout.totalBytes(), width + 3u);

            std::vector<std::uint8_t> expected = input;
            an::ExecutionContext oracle = makeExecution(engine, referenceBackend());
            ASSERT_TRUE(an::swapImageByteOrder(oracle, sampleType, expected.data(), layout).ok());

            for (const ForcedBackend& forced : optimized)
            {
                std::vector<std::uint8_t> actual = input;
                an::ExecutionContext execution = makeExecution(engine, forced);
                expectDispatchedTo(execution, forced);
                ASSERT_TRUE(an::swapImageByteOrder(execution, sampleType, actual.data(), layout).ok());
                EXPECT_EQ(actual, expected) << forced.name << " disagreed with the oracle on "
                                            << an::sampleTypeName(sampleType) << " width " << width;

                // Its own inverse, on every backend. A swap that lost the last partial vector would
                // agree with a reference that lost the same tail and still fail this.
                an::ExecutionContext again = makeExecution(engine, forced);
                ASSERT_TRUE(an::swapImageByteOrder(again, sampleType, actual.data(), layout).ok());
                EXPECT_EQ(actual, input) << forced.name << " did not undo its own swap on "
                                         << an::sampleTypeName(sampleType) << " width " << width;
            }
        }
    }
}

TEST(LayoutBackendTest, TheSweepActuallyRanSomethingOtherThanTheOracle)
{
    // The case that keeps every case above honest. A build in which no optimized backend can be
    // forced makes each of them skip, and a reader seeing four skips should be able to tell that
    // from a reader seeing four passes.
    const std::vector<ForcedBackend> optimized = compiledBackends();
    if (optimized.empty())
    {
        GTEST_SKIP() << "this build compiled no optimized kernels this host can run";
    }

    an::EngineContext engine = makeEngine();
    for (const ForcedBackend& forced : optimized)
    {
        an::ExecutionContext execution = makeExecution(engine, forced);
        EXPECT_NE(an::instructionSetOfDispatchedCode(execution), an::InstructionSet::Scalar)
            << forced.name << " dispatched to the oracle, so every differential case above compared it with itself";
    }
}
