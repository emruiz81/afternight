// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageConversion.h.
//
// A compiled backend that agrees with itself proves nothing, so almost every case here runs the same
// work twice - once forced to `ComputeBackend::Reference`, once left automatic - and compares the two
// outputs byte for byte. That comparison is only meaningful because the two backends genuinely point
// at different compiled code, which the dispatch cases assert directly rather than assume.
//
// **Every ordered pair, at every tail and every stride.** A conversion kernel that is wrong is
// usually wrong at the end of a row - the samples a vectorised loop handles separately - or at a row
// whose stride is not its width. The differential sweep covers all sixty-four pairs over widths that
// are and are not multiples of the vector width, over padded and tight layouts, and over planar and
// interleaved arrangements.
//
// **The counters are a property of the samples, not of the schedule.** The same conversion run on
// one thread and on several reports the same three numbers, which is what makes them worth reporting
// at all.
//
// **Nothing may alias.** A destination overlapping its source would make the answer depend on the
// order the rows were scheduled in, so it is refused rather than being right on one thread and wrong
// on eight.
//
// **Integer storage is converted in one fused walk.** The allocation case fills the context's
// entire memory budget with the source and destination storages, then proves that neither the
// reference nor the compiled CPU kernel asks for, owns or retains a hidden full-frame working image.

#include <AfterNightEngine/Image/ImageConversion.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/BackendDispatch.h>
#include <AfterNightEngine/Runtime/CpuCapabilities.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
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

/// The approved defaults, which the fast bodies specialise for.
constexpr an::ConversionPolicy kDefaults{};

/// A policy no fast body specialises for, so the compiled backend falls through to the reference.
constexpr an::ConversionPolicy kUnusualPolicy{.rounding = an::RoundingMode::Floor,
                                              .domain = an::DomainMapping::CodeValue};

/// An engine context every case in this file starts from.
///
/// @param maxConcurrency the ceiling to configure.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint32_t maxConcurrency = 4u)
{
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// One operation, optionally forced onto the scalar reference.
///
/// @param engine the context to start it in.
/// @param backend which implementation to require.
/// @param maxConcurrency the operation's own ceiling.
/// @param stopToken how the operation may be stopped.
/// @return the execution context, which is valid or the case has already failed.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 an::ComputeBackend backend = an::ComputeBackend::Automatic,
                                                 std::uint32_t maxConcurrency = an::kUseContextConcurrency,
                                                 std::stop_token stopToken = std::stop_token{})
{
    an::ExecutionOptions options;
    options.backend = backend;
    options.maxConcurrency = maxConcurrency;
    options.stopToken = std::move(stopToken);

    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
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
/// @tparam SampleT the representation.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingSamples how many samples of padding follow each row, so a case can exercise a
///        stride that is not the row.
/// @return the layout.
template <typename SampleT>
[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                                       bool interleaved, std::uint32_t rowPaddingSamples = 0u)
{
    const std::uint32_t samplesPerPixel = interleaved ? channelCount : 1u;
    const std::uint32_t planeCount = interleaved ? 1u : channelCount;
    const an::ImageLayoutRequest request{.extent = extentOf(width, height, channelCount),
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(SampleT)),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride =
        (static_cast<std::uint64_t>(width) * samplesPerPixel + rowPaddingSamples) * sizeof(SampleT);
    const std::uint64_t planeStride = rowStride * height;
    const std::optional<an::ImageLayout> made =
        an::ImageLayout::createWithStrides(request, rowStride, planeStride, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A buffer large enough for a layout, filled with a deterministic pattern.
///
/// The pattern walks the destination's whole range rather than a plausible image, because the values
/// a conversion gets wrong are its extrema and its halfway points rather than its midtones.
///
/// @tparam SampleT the representation.
/// @param layout where the samples sit.
/// @return the buffer.
template <typename SampleT> [[nodiscard]] std::vector<SampleT> patternedBuffer(const an::ImageLayout& layout)
{
    std::vector<SampleT> buffer(static_cast<std::size_t>(layout.totalBytes() / sizeof(SampleT)) + 1u, SampleT{});
    const double lowest = an::kSampleMinimumOf<SampleT>;
    const double highest = an::kSampleMaximumOf<SampleT>;
    for (std::size_t index = 0u; index < buffer.size(); ++index)
    {
        const double position = static_cast<double>(index % 97u) / 96.0;
        if constexpr (std::is_floating_point_v<SampleT>)
        {
            // A quarter past full scale on either side, so the saturating paths are exercised, and
            // one of each non-finite value every ninety-seven samples, so the mapping every pair
            // has to make for them is exercised too. Without those three the sweep would never
            // reach the branch a conversion is most likely to get wrong, and a compiled kernel that
            // dropped the non-finite count would agree with the reference everywhere it was asked.
            if (index % 97u == 3u)
            {
                buffer[index] = std::numeric_limits<SampleT>::quiet_NaN();
            }
            else if (index % 97u == 5u)
            {
                buffer[index] = std::numeric_limits<SampleT>::infinity();
            }
            else if (index % 97u == 7u)
            {
                buffer[index] = -std::numeric_limits<SampleT>::infinity();
            }
            else
            {
                buffer[index] = static_cast<SampleT>(-0.25 + position * 1.5);
            }
        }
        else
        {
            buffer[index] = static_cast<SampleT>(lowest + position * (highest - lowest));
        }
    }
    return buffer;
}

/// Converts one image on one backend and reports what it wrote and what it met.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param engine the context to run in.
/// @param backend which implementation to require.
/// @param source the samples to convert.
/// @param sourceLayout where they sit.
/// @param destinationLayout where the converted ones go.
/// @param policy the five decisions.
/// @param counters where the counts are written.
/// @param concurrency the operation's thread ceiling.
/// @return the destination buffer, or an empty one when the conversion failed.
template <typename SourceT, typename DestT>
[[nodiscard]] std::vector<DestT> convertOn(const an::EngineContext& engine, an::ComputeBackend backend,
                                           const std::vector<SourceT>& source, const an::ImageLayout& sourceLayout,
                                           const an::ImageLayout& destinationLayout, const an::ConversionPolicy& policy,
                                           an::ConversionCounters& counters,
                                           std::uint32_t concurrency = an::kUseContextConcurrency)
{
    an::ExecutionContext execution = makeExecution(engine, backend, concurrency);
    std::vector<DestT> destination(static_cast<std::size_t>(destinationLayout.totalBytes() / sizeof(DestT)) + 1u,
                                   DestT{});
    const an::Status converted =
        an::convertImageSamples(execution, an::kSampleTypeOf<SourceT>, source.data(), sourceLayout,
                                an::kSampleTypeOf<DestT>, destination.data(), destinationLayout, policy, counters);
    EXPECT_TRUE(converted.ok()) << converted.message();
    return converted.ok() ? destination : std::vector<DestT>{};
}

/// Runs one ordered pair on both backends and demands they agree exactly.
///
/// @tparam SourceT the representation converted from.
/// @tparam DestT the representation converted to.
/// @param engine the context to run in.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @param policy the five decisions.
template <typename SourceT, typename DestT>
void expectBackendsAgree(const an::EngineContext& engine, std::uint32_t width, std::uint32_t height,
                         std::uint32_t channelCount, bool interleaved, std::uint32_t rowPaddingSamples,
                         const an::ConversionPolicy& policy)
{
    const an::ImageLayout sourceLayout = layoutOf<SourceT>(width, height, channelCount, interleaved, rowPaddingSamples);
    const an::ImageLayout destinationLayout = layoutOf<DestT>(width, height, channelCount, interleaved, 0u);
    const std::vector<SourceT> source = patternedBuffer<SourceT>(sourceLayout);

    an::ConversionCounters referenceCounters;
    const std::vector<DestT> reference = convertOn<SourceT, DestT>(
        engine, an::ComputeBackend::Reference, source, sourceLayout, destinationLayout, policy, referenceCounters);
    an::ConversionCounters compiledCounters;
    const std::vector<DestT> compiled = convertOn<SourceT, DestT>(engine, an::ComputeBackend::Cpu, source, sourceLayout,
                                                                  destinationLayout, policy, compiledCounters);

    ASSERT_EQ(reference.size(), compiled.size());
    EXPECT_EQ(std::memcmp(reference.data(), compiled.data(), reference.size() * sizeof(DestT)), 0)
        << "pair " << an::sampleTypeName(an::kSampleTypeOf<SourceT>) << " to "
        << an::sampleTypeName(an::kSampleTypeOf<DestT>) << " at width " << width;
    EXPECT_EQ(referenceCounters, compiledCounters);
}

/// Runs one source representation against every destination on both backends.
///
/// @tparam SourceT the representation converted from.
/// @param engine the context to run in.
/// @param width the width in pixels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @param policy the five decisions.
template <typename SourceT>
void expectRowOfMatrixAgrees(const an::EngineContext& engine, std::uint32_t width, bool interleaved,
                             std::uint32_t rowPaddingSamples, const an::ConversionPolicy& policy)
{
    expectBackendsAgree<SourceT, std::uint8_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, std::int8_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, std::uint16_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, std::int16_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, std::uint32_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, std::int32_t>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, float>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
    expectBackendsAgree<SourceT, double>(engine, width, 3u, 2u, interleaved, rowPaddingSamples, policy);
}

/// Runs the whole matrix on both backends.
///
/// @param engine the context to run in.
/// @param width the width in pixels.
/// @param interleaved whether one plane holds every channel.
/// @param rowPaddingSamples how many samples of padding follow each row.
/// @param policy the five decisions.
void expectMatrixAgrees(const an::EngineContext& engine, std::uint32_t width, bool interleaved,
                        std::uint32_t rowPaddingSamples, const an::ConversionPolicy& policy = kDefaults)
{
    expectRowOfMatrixAgrees<std::uint8_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<std::int8_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<std::uint16_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<std::int16_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<std::uint32_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<std::int32_t>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<float>(engine, width, interleaved, rowPaddingSamples, policy);
    expectRowOfMatrixAgrees<double>(engine, width, interleaved, rowPaddingSamples, policy);
}

} // namespace

TEST(ConversionDispatchTest, TheTwoBackendsAreGenuinelyDifferentCompiledCode)
{
    // Every differential case below rests on this. If the two selections resolved to one table, the
    // comparisons would be a compiled kernel agreeing with itself.
    // One operation at a time: an execution context asking for the whole engine context leaves the
    // next one nothing, and that admission rule has nothing to do with what is being tested here.
    an::EngineContext engine = makeEngineContext();
    {
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        const an::Result<an::BackendSelection> onReference = an::selectBackend(reference);
        ASSERT_TRUE(onReference.hasValue());
        EXPECT_EQ(onReference->backend, an::ComputeBackend::Reference);
        EXPECT_EQ(onReference->instructionSet, an::InstructionSet::Scalar);
        EXPECT_EQ(an::instructionSetOfDispatchedCode(reference), an::InstructionSet::Scalar);
    }
    {
        an::ExecutionContext automatic = makeExecution(engine, an::ComputeBackend::Automatic);
        const an::Result<an::BackendSelection> onCpu = an::selectBackend(automatic);
        ASSERT_TRUE(onCpu.hasValue());
        EXPECT_EQ(onCpu->backend, an::ComputeBackend::Cpu);
        EXPECT_EQ(onCpu->instructionSet, an::instructionSetOfDispatchedCode(automatic));
        EXPECT_NE(onCpu->instructionSet, an::InstructionSet::Automatic);
        EXPECT_NE(onCpu->instructionSet, an::InstructionSet::Scalar);
    }
}

TEST(ConversionDifferentialTest, EveryOrderedPairAgreesAcrossBackendsOnATightPlanarImage)
{
    an::EngineContext engine = makeEngineContext();
    expectMatrixAgrees(engine, 64u, false, 0u);
}

TEST(ConversionDifferentialTest, EveryOrderedPairAgreesOnATailThatIsNotAVectorWidth)
{
    an::EngineContext engine = makeEngineContext();
    expectMatrixAgrees(engine, 37u, false, 0u);
    expectMatrixAgrees(engine, 1u, false, 0u);
}

TEST(ConversionDifferentialTest, EveryOrderedPairAgreesWhenTheSourceRowsArePadded)
{
    an::EngineContext engine = makeEngineContext();
    expectMatrixAgrees(engine, 37u, false, 5u);
}

TEST(ConversionDifferentialTest, EveryOrderedPairAgreesOnAnInterleavedImage)
{
    an::EngineContext engine = makeEngineContext();
    expectMatrixAgrees(engine, 37u, true, 3u);
}

TEST(ConversionDifferentialTest, EveryOrderedPairAgreesUnderAPolicyNoFastBodySpecialisesFor)
{
    // The compiled backend falls through to the reference for a policy it has no fast body for, so
    // this case proves the fallback is wired rather than that the two happen to agree.
    an::EngineContext engine = makeEngineContext();
    expectMatrixAgrees(engine, 37u, false, 0u, kUnusualPolicy);
}

TEST(ConversionDifferentialTest, TheDispatchedConversionAgreesWithTheScalarRunSampleForSample)
{
    // The other direction of the same claim: not that the two backends agree with each other, but
    // that both agree with the run function the reference matrix is written as.
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout sourceLayout = layoutOf<float>(29u, 4u, 1u, false, 0u);
    const an::ImageLayout destinationLayout = layoutOf<std::uint16_t>(29u, 4u, 1u, false, 0u);
    const std::vector<float> source = patternedBuffer<float>(sourceLayout);

    an::ConversionCounters imageCounters;
    const std::vector<std::uint16_t> converted = convertOn<float, std::uint16_t>(
        engine, an::ComputeBackend::Automatic, source, sourceLayout, destinationLayout, kDefaults, imageCounters);
    ASSERT_FALSE(converted.empty());

    std::vector<std::uint16_t> expected(converted.size(), 0u);
    an::ConversionCounters runCounters;
    const std::uint64_t rowSamples = 29u;
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        const std::size_t sourceOffset = static_cast<std::size_t>(y * sourceLayout.rowStrideBytes() / sizeof(float));
        const std::size_t destinationOffset =
            static_cast<std::size_t>(y * destinationLayout.rowStrideBytes() / sizeof(std::uint16_t));
        ASSERT_TRUE(
            (an::convertSampleRun<float, std::uint16_t>(
                 std::span<const float>{source.data() + sourceOffset, static_cast<std::size_t>(rowSamples)},
                 std::span<std::uint16_t>{expected.data() + destinationOffset, static_cast<std::size_t>(rowSamples)},
                 kDefaults, runCounters))
                .ok());
    }
    EXPECT_EQ(std::memcmp(expected.data(), converted.data(), expected.size() * sizeof(std::uint16_t)), 0);
    EXPECT_EQ(imageCounters, runCounters);
}

/// Converts the same Float32 samples to an unsigned code on both backends and demands they agree exactly.
///
/// @tparam DestT the unsigned eight- or sixteen-bit representation.
/// @param engine the context to run in.
/// @param source the samples, one row.
template <typename DestT> void expectNarrowingAgrees(const an::EngineContext& engine, const std::vector<float>& source)
{
    const an::ImageLayout sourceLayout = layoutOf<float>(static_cast<std::uint32_t>(source.size()), 1u, 1u, false);
    const an::ImageLayout destinationLayout = layoutOf<DestT>(static_cast<std::uint32_t>(source.size()), 1u, 1u, false);
    an::ConversionCounters referenceCounters;
    const std::vector<DestT> reference = convertOn<float, DestT>(
        engine, an::ComputeBackend::Reference, source, sourceLayout, destinationLayout, kDefaults, referenceCounters);
    an::ConversionCounters compiledCounters;
    const std::vector<DestT> compiled = convertOn<float, DestT>(engine, an::ComputeBackend::Cpu, source, sourceLayout,
                                                                destinationLayout, kDefaults, compiledCounters);
    ASSERT_EQ(reference.size(), compiled.size());
    std::size_t first = reference.size();
    for (std::size_t index = 0u; index < reference.size(); ++index)
    {
        if (reference[index] != compiled[index])
        {
            first = index;
            break;
        }
    }
    EXPECT_EQ(first, reference.size()) << "the first sample narrowed otherwise is " << source[first] << ", "
                                       << static_cast<int>(compiled[first]) << " where the reference wrote "
                                       << static_cast<int>(reference[first]);
    EXPECT_EQ(referenceCounters, compiledCounters);
}

// A sample half a code from two codes is the one the rounding decides, and the two Float32 values on either side of
// it the ones a wrong product or a wrong comparison moves: at every code of both unsigned representations, those
// three, the code itself, and the extremes and the values that hold no number, the compiled narrowing writes what the
// reference writes and counts what it counts.
TEST(ConversionDifferentialTest, AFloat32SampleNearHalfACodeNarrowsAsTheReferenceDoesAtEveryCode)
{
    an::EngineContext engine = makeEngineContext();
    const auto samplesAbout = [](double fullScale) {
        std::vector<float> samples;
        for (double code = 0.0; code <= fullScale; code += 1.0)
        {
            const float exact = static_cast<float>(code / fullScale);
            const float half = static_cast<float>((code + 0.5) / fullScale);
            samples.insert(samples.end(), {exact, half, std::nextafter(half, 0.0f), std::nextafter(half, 2.0f)});
        }
        samples.insert(samples.end(),
                       {-0.0f, -1.0e-30f, -0.5f, 1.0f, 1.0000001f, 2.0f, 1.0e30f, -1.0e30f,
                        std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                        (std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)()});
        return samples;
    };
    expectNarrowingAgrees<std::uint16_t>(engine, samplesAbout(65535.0));
    expectNarrowingAgrees<std::uint8_t>(engine, samplesAbout(255.0));
}

/// Narrows one row of floating samples to an integer representation on both backends under a policy, and demands
/// the same samples and the same counts.
template <typename SourceT, typename DestT>
void expectFloatingNarrowingAgrees(const an::EngineContext& engine, const std::vector<SourceT>& source,
                                   const an::ConversionPolicy& policy)
{
    const auto width = static_cast<std::uint32_t>(source.size());
    const an::ImageLayout sourceLayout = layoutOf<SourceT>(width, 1u, 1u, false);
    const an::ImageLayout destinationLayout = layoutOf<DestT>(width, 1u, 1u, false);
    an::ConversionCounters referenceCounters;
    const std::vector<DestT> reference = convertOn<SourceT, DestT>(
        engine, an::ComputeBackend::Reference, source, sourceLayout, destinationLayout, policy, referenceCounters);
    an::ConversionCounters compiledCounters;
    const std::vector<DestT> compiled = convertOn<SourceT, DestT>(engine, an::ComputeBackend::Cpu, source, sourceLayout,
                                                                  destinationLayout, policy, compiledCounters);
    ASSERT_EQ(reference.size(), compiled.size());
    std::size_t first = reference.size();
    for (std::size_t index = 0u; index < reference.size(); ++index)
    {
        if (reference[index] != compiled[index])
        {
            first = index;
            break;
        }
    }
    EXPECT_EQ(first, reference.size()) << "the first sample narrowed otherwise is "
                                       << (first < source.size() ? static_cast<double>(source[first]) : 0.0) << ", "
                                       << static_cast<std::int64_t>(first < compiled.size() ? compiled[first] : DestT{})
                                       << " where the reference wrote "
                                       << static_cast<std::int64_t>(first < reference.size() ? reference[first]
                                                                                             : DestT{});
    EXPECT_EQ(referenceCounters, compiledCounters);
}

/// Floating samples about half a code of a representation, as fractions of its full scale or as codes: at codes
/// spread over its whole range, every code for the narrower ones, the code itself, the quarter and half codes either
/// side and the floating values next to each, then the extremes and the values that hold no number.
template <typename SourceT, typename DestT> [[nodiscard]] std::vector<SourceT> samplesAboutHalfCodes(bool fullScale)
{
    const double low = an::kSampleMinimumOf<DestT>;
    const double high = an::kSampleMaximumOf<DestT>;
    const double scale = fullScale ? high : 1.0;
    const double span = high - low;
    const std::uint32_t steps = span < 70000.0 ? static_cast<std::uint32_t>(span) : 6007u;
    std::vector<SourceT> samples;
    for (std::uint32_t step = 0u; step <= steps; ++step)
    {
        const double code = std::floor(low + span * static_cast<double>(step) / static_cast<double>(steps));
        for (const double offset : {0.0, 0.5, -0.5, 0.25, 1.5})
        {
            const auto value = static_cast<SourceT>((code + offset) / scale);
            samples.insert(samples.end(), {value, std::nextafter(value, static_cast<SourceT>(-1e30)),
                                           std::nextafter(value, static_cast<SourceT>(1e30))});
        }
    }
    samples.insert(samples.end(),
                   {static_cast<SourceT>(-0.0), std::numeric_limits<SourceT>::denorm_min(),
                    static_cast<SourceT>(low / scale - 1.0), static_cast<SourceT>(high / scale + 1.0),
                    static_cast<SourceT>(1e30), static_cast<SourceT>(-1e30), std::numeric_limits<SourceT>::quiet_NaN(),
                    std::numeric_limits<SourceT>::infinity(), -std::numeric_limits<SourceT>::infinity(),
                    (std::numeric_limits<SourceT>::max)(), -(std::numeric_limits<SourceT>::max)()});
    return samples;
}

/// Every integer representation narrowed from one floating one, under the defaults and as code values.
template <typename SourceT> void expectEveryIntegerNarrowingAgrees(const an::EngineContext& engine)
{
    constexpr an::ConversionPolicy kCodes{.domain = an::DomainMapping::CodeValue};
    for (const bool fullScale : {true, false})
    {
        SCOPED_TRACE(fullScale ? "full scale" : "code values");
        const an::ConversionPolicy& policy = fullScale ? kDefaults : kCodes;
        expectFloatingNarrowingAgrees<SourceT, std::uint8_t>(
            engine, samplesAboutHalfCodes<SourceT, std::uint8_t>(fullScale), policy);
        expectFloatingNarrowingAgrees<SourceT, std::int8_t>(
            engine, samplesAboutHalfCodes<SourceT, std::int8_t>(fullScale), policy);
        expectFloatingNarrowingAgrees<SourceT, std::uint16_t>(
            engine, samplesAboutHalfCodes<SourceT, std::uint16_t>(fullScale), policy);
        expectFloatingNarrowingAgrees<SourceT, std::int16_t>(
            engine, samplesAboutHalfCodes<SourceT, std::int16_t>(fullScale), policy);
        expectFloatingNarrowingAgrees<SourceT, std::uint32_t>(
            engine, samplesAboutHalfCodes<SourceT, std::uint32_t>(fullScale), policy);
        expectFloatingNarrowingAgrees<SourceT, std::int32_t>(
            engine, samplesAboutHalfCodes<SourceT, std::int32_t>(fullScale), policy);
    }
}

// Every floating sample narrowed to an integer under the defaults, and every one narrowed as a code value - how a
// pointwise result and a signed or 32-bit integer's working copy come back - lands on the reference's code and counts
// what it counts: about half a code at codes across each representation, at both ends, beyond them and where there is
// no number.
TEST(ConversionDifferentialTest, AFloatingSampleNarrowsToEveryIntegerAsTheReferenceDoes)
{
    an::EngineContext engine = makeEngineContext();
    {
        SCOPED_TRACE("Float32");
        expectEveryIntegerNarrowingAgrees<float>(engine);
    }
    {
        SCOPED_TRACE("Float64");
        expectEveryIntegerNarrowingAgrees<double>(engine);
    }
}

TEST(ConversionDifferentialTest, TheCountersAreTheSameOnOneThreadAndOnSeveral)
{
    an::EngineContext engine = makeEngineContext(4u);
    const an::ImageLayout sourceLayout = layoutOf<float>(101u, 64u, 3u, false, 0u);
    const an::ImageLayout destinationLayout = layoutOf<std::uint8_t>(101u, 64u, 3u, false, 0u);
    const std::vector<float> source = patternedBuffer<float>(sourceLayout);

    an::ConversionCounters single;
    const std::vector<std::uint8_t> onOne = convertOn<float, std::uint8_t>(
        engine, an::ComputeBackend::Automatic, source, sourceLayout, destinationLayout, kDefaults, single, 1u);
    an::ConversionCounters several;
    const std::vector<std::uint8_t> onMany = convertOn<float, std::uint8_t>(
        engine, an::ComputeBackend::Automatic, source, sourceLayout, destinationLayout, kDefaults, several, 4u);

    ASSERT_EQ(onOne.size(), onMany.size());
    EXPECT_EQ(std::memcmp(onOne.data(), onMany.data(), onOne.size()), 0);
    EXPECT_EQ(single, several);
    EXPECT_EQ(single.samplesConverted, 101u * 64u * 3u);
    EXPECT_GT(single.saturatedSamples, 0u);
}

TEST(ConversionRefusalTest, AnOperationThatHoldsNothingConvertsNothing)
{
    const an::ExecutionContext empty;
    const an::ImageLayout layout = layoutOf<float>(4u, 4u, 1u, false, 0u);
    std::vector<float> source(64u, 0.0f);
    std::vector<float> destination(64u, 0.0f);
    an::ConversionCounters counters;
    const an::Status refused =
        an::convertImageSamples(empty, an::SampleType::Float32, source.data(), layout, an::SampleType::Float32,
                                destination.data(), layout, kDefaults, counters);
    EXPECT_EQ(refused.code(), an::ErrorCode::NotInitialized);
}

TEST(ConversionRefusalTest, ANullBufferIsNamedRatherThanDereferenced)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<float>(4u, 4u, 1u, false, 0u);
    std::vector<float> buffer(64u, 0.0f);
    an::ConversionCounters counters;

    EXPECT_EQ(an::convertImageSamples(execution, an::SampleType::Float32, nullptr, layout, an::SampleType::Float32,
                                      buffer.data(), layout, kDefaults, counters)
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::convertImageSamples(execution, an::SampleType::Float32, buffer.data(), layout,
                                      an::SampleType::Float32, nullptr, layout, kDefaults, counters)
                  .code(),
              an::ErrorCode::InvalidArgument);
}

TEST(ConversionRefusalTest, ALayoutWhoseElementSizeIsNotItsRepresentationIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout floats = layoutOf<float>(4u, 4u, 1u, false, 0u);
    std::vector<float> buffer(64u, 0.0f);
    an::ConversionCounters counters;

    const an::Status refused =
        an::convertImageSamples(execution, an::SampleType::UInt8, buffer.data(), floats, an::SampleType::Float32,
                                buffer.data(), floats, kDefaults, counters);
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ConversionRefusalTest, TwoImagesOfDifferentShapesAreRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout wide = layoutOf<float>(8u, 4u, 1u, false, 0u);
    const an::ImageLayout narrow = layoutOf<std::uint8_t>(4u, 4u, 1u, false, 0u);
    std::vector<float> source(256u, 0.0f);
    std::vector<std::uint8_t> destination(256u, 0u);
    an::ConversionCounters counters;

    const an::Status refused =
        an::convertImageSamples(execution, an::SampleType::Float32, source.data(), wide, an::SampleType::UInt8,
                                destination.data(), narrow, kDefaults, counters);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ConversionRefusalTest, AnArrangementChangeIsARepackRatherThanAConversion)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf<float>(8u, 4u, 3u, false, 0u);
    const an::ImageLayout interleaved = layoutOf<float>(8u, 4u, 3u, true, 0u);
    std::vector<float> source(1024u, 0.0f);
    std::vector<float> destination(1024u, 0.0f);
    an::ConversionCounters counters;

    const an::Status refused =
        an::convertImageSamples(execution, an::SampleType::Float32, source.data(), planar, an::SampleType::Float32,
                                destination.data(), interleaved, kDefaults, counters);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ConversionRefusalTest, ADestinationThatSharesMemoryWithItsSourceIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<float>(8u, 4u, 1u, false, 0u);
    std::vector<float> buffer(64u, 0.0f);
    an::ConversionCounters counters;

    const an::Status refused =
        an::convertImageSamples(execution, an::SampleType::Float32, buffer.data(), layout, an::SampleType::Float32,
                                buffer.data(), layout, kDefaults, counters);
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ConversionRefusalTest, ARejectingPolicyStopsTheWholeConversionAndSaysWhere)
{
    an::EngineContext engine = makeEngineContext(1u);
    an::ExecutionContext execution = makeExecution(engine, an::ComputeBackend::Automatic, 1u);
    const an::ImageLayout sourceLayout = layoutOf<float>(8u, 2u, 1u, false, 0u);
    const an::ImageLayout destinationLayout = layoutOf<std::uint8_t>(8u, 2u, 1u, false, 0u);
    std::vector<float> source(32u, 0.5f);
    source[9u] = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::uint8_t> destination(32u, 0u);
    an::ConversionCounters counters;

    const an::ConversionPolicy policy{.nonFinite = an::NonFinitePolicy::Reject};
    const an::Status refused =
        an::convertImageSamples(execution, an::SampleType::Float32, source.data(), sourceLayout, an::SampleType::UInt8,
                                destination.data(), destinationLayout, policy, counters);
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidData);
    const an::StatusContextField* const where = refused.findContext(an::ContextKey::Completed);
    ASSERT_NE(where, nullptr);
    EXPECT_EQ(where->count, 9u);
}

TEST(ConversionCancellationTest, AStoppedConversionGivesUpAndSaysSo)
{
    an::EngineContext engine = makeEngineContext();
    std::stop_source stopper;
    stopper.request_stop();
    an::ExecutionContext execution =
        makeExecution(engine, an::ComputeBackend::Automatic, an::kUseContextConcurrency, stopper.get_token());

    const an::ImageLayout sourceLayout = layoutOf<float>(64u, 64u, 1u, false, 0u);
    const an::ImageLayout destinationLayout = layoutOf<std::uint8_t>(64u, 64u, 1u, false, 0u);
    std::vector<float> source(8192u, 0.5f);
    std::vector<std::uint8_t> destination(8192u, 0u);
    an::ConversionCounters counters;

    const an::Status stopped =
        an::convertImageSamples(execution, an::SampleType::Float32, source.data(), sourceLayout, an::SampleType::UInt8,
                                destination.data(), destinationLayout, kDefaults, counters);
    EXPECT_TRUE(stopped.isCancellation());
    EXPECT_EQ(stopped.code(), an::ErrorCode::Cancelled);
}

TEST(ConversionTypedTest, TheTypedSpellingReachesTheSameCodeAsTheUntypedOne)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout sourceLayout = layoutOf<std::uint16_t>(9u, 3u, 1u, false, 0u);
    const an::ImageLayout destinationLayout = layoutOf<float>(9u, 3u, 1u, false, 0u);
    const std::vector<std::uint16_t> source = patternedBuffer<std::uint16_t>(sourceLayout);
    std::vector<float> destination(source.size(), 0.0f);

    const std::optional<an::ImageView<const std::uint16_t>> sourceView =
        an::ImageView<const std::uint16_t>::create(std::span<const std::uint16_t>{source}, sourceLayout);
    const std::optional<an::ImageView<float>> destinationView =
        an::ImageView<float>::create(std::span<float>{destination}, destinationLayout);
    ASSERT_TRUE(sourceView.has_value());
    ASSERT_TRUE(destinationView.has_value());

    an::ConversionCounters counters;
    ASSERT_TRUE(
        (an::convertImage<std::uint16_t, float>(execution, *sourceView, *destinationView, kDefaults, counters)).ok());
    EXPECT_EQ(counters.samplesConverted, 27u);
    EXPECT_EQ(destination[0], (an::convertSample<std::uint16_t, float>(source[0], kDefaults)).value);
}

TEST(ConversionTypedTest, IntegerStorageKernelsAllocateNoHiddenFullFrameWorkingImage)
{
    constexpr std::uint16_t kSourceValue = 49152u;
    constexpr std::array kBackends{an::ComputeBackend::Reference, an::ComputeBackend::Cpu};
    const an::ImageLayout sourceLayout = layoutOf<std::uint16_t>(257u, 17u, 3u, false, 3u);
    const an::ImageLayout destinationLayout = layoutOf<std::int16_t>(257u, 17u, 3u, false, 3u);
    const std::uint64_t storageBytes = sourceLayout.totalBytes() + destinationLayout.totalBytes();

    for (const an::ComputeBackend backend : kBackends)
    {
        an::EngineContextOptions engineOptions;
        engineOptions.maxConcurrency = 1u;
        engineOptions.limits = an::defaultEngineLimits();
        engineOptions.limits.memoryBudgetBytes = storageBytes;
        an::Result<an::EngineContext> madeEngine = an::EngineContext::create(engineOptions);
        ASSERT_TRUE(madeEngine.hasValue());
        an::EngineContext engine = std::move(*madeEngine);

        an::Result<an::ImageStorage<std::uint16_t>> source =
            an::ImageStorage<std::uint16_t>::createFilled(engine, sourceLayout, kSourceValue);
        ASSERT_TRUE(source.hasValue());
        an::Result<an::ImageStorage<std::int16_t>> destination =
            an::ImageStorage<std::int16_t>::createFilled(engine, destinationLayout, 0);
        ASSERT_TRUE(destination.hasValue());

        an::ExecutionContext execution = makeExecution(engine, backend, 1u);
        an::MemoryResource* const memory = engine.memoryResource();
        ASSERT_NE(memory, nullptr);
        const an::MemoryCounters before = memory->counters();
        ASSERT_EQ(before.liveBytes, storageBytes);
        ASSERT_EQ(before.liveAllocations, 2u);
        ASSERT_EQ(before.peakBytes, storageBytes);
        ASSERT_EQ(before.totalBytes, storageBytes);
        ASSERT_EQ(before.totalAllocations, 2u);
        ASSERT_EQ(before.deniedAllocations, 0u);

        const std::optional<an::ImageView<const std::uint16_t>> sourceView = std::as_const(*source).view();
        const std::optional<an::ImageView<std::int16_t>> destinationView = destination->view();
        ASSERT_TRUE(sourceView.has_value());
        ASSERT_TRUE(destinationView.has_value());

        an::ConversionCounters conversionCounters;
        ASSERT_TRUE((an::convertImage<std::uint16_t, std::int16_t>(execution, *sourceView, *destinationView, kDefaults,
                                                                   conversionCounters))
                        .ok());

        const an::MemoryCounters after = memory->counters();
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_EQ(after.liveAllocations, before.liveAllocations);
        EXPECT_EQ(after.peakBytes, before.peakBytes);
        EXPECT_EQ(after.totalBytes, before.totalBytes);
        EXPECT_EQ(after.totalAllocations, before.totalAllocations);
        EXPECT_EQ(after.deniedAllocations, before.deniedAllocations);
        EXPECT_EQ(conversionCounters.samplesConverted, sourceLayout.extent().sampleCount());
        EXPECT_EQ(destination->data()[0],
                  (an::convertSample<std::uint16_t, std::int16_t>(kSourceValue, kDefaults)).value);
    }
}

TEST(RepackTest, PlanarAndInterleavedRoundTripWithoutChangingASample)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf<std::uint16_t>(13u, 5u, 3u, false, 2u);
    const an::ImageLayout interleaved = layoutOf<std::uint16_t>(13u, 5u, 3u, true, 1u);
    const std::vector<std::uint16_t> source = patternedBuffer<std::uint16_t>(planar);

    std::vector<std::uint16_t> packed(static_cast<std::size_t>(interleaved.totalBytes() / sizeof(std::uint16_t)) + 1u,
                                      0u);
    ASSERT_TRUE(
        an::repackImageSamples(execution, an::SampleType::UInt16, source.data(), planar, packed.data(), interleaved)
            .ok());

    std::vector<std::uint16_t> unpacked(source.size(), 0u);
    ASSERT_TRUE(
        an::repackImageSamples(execution, an::SampleType::UInt16, packed.data(), interleaved, unpacked.data(), planar)
            .ok());

    for (std::uint32_t plane = 0u; plane < 3u; ++plane)
    {
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            const std::size_t offset = static_cast<std::size_t>(
                (plane * planar.planeStrideBytes() + y * planar.rowStrideBytes()) / sizeof(std::uint16_t));
            EXPECT_EQ(std::memcmp(source.data() + offset, unpacked.data() + offset, 13u * sizeof(std::uint16_t)), 0)
                << "plane " << plane << " row " << y;
        }
    }
}

TEST(RepackTest, TheInterleavedFormPutsAPixelSamplesTogether)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf<std::uint8_t>(2u, 1u, 3u, false, 0u);
    const an::ImageLayout interleaved = layoutOf<std::uint8_t>(2u, 1u, 3u, true, 0u);
    const std::vector<std::uint8_t> source{1u, 2u, 3u, 4u, 5u, 6u};
    std::vector<std::uint8_t> packed(6u, 0u);

    ASSERT_TRUE(
        an::repackImageSamples(execution, an::SampleType::UInt8, source.data(), planar, packed.data(), interleaved)
            .ok());
    const std::vector<std::uint8_t> expected{1u, 3u, 5u, 2u, 4u, 6u};
    EXPECT_EQ(packed, expected);
}

TEST(RepackTest, ARepackThatOverlapsOrChangesShapeIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout planar = layoutOf<std::uint8_t>(4u, 2u, 3u, false, 0u);
    const an::ImageLayout interleaved = layoutOf<std::uint8_t>(4u, 2u, 3u, true, 0u);
    const an::ImageLayout other = layoutOf<std::uint8_t>(5u, 2u, 3u, true, 0u);
    std::vector<std::uint8_t> buffer(64u, 0u);
    std::vector<std::uint8_t> second(64u, 0u);

    EXPECT_EQ(
        an::repackImageSamples(execution, an::SampleType::UInt8, buffer.data(), planar, buffer.data(), interleaved)
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(
        an::repackImageSamples(execution, an::SampleType::UInt8, buffer.data(), planar, second.data(), other).code(),
        an::ErrorCode::IncompatibleArguments);
}

TEST(ByteOrderTest, SwappingTwiceLeavesEveryByteAsItWas)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<std::uint32_t>(11u, 3u, 2u, false, 2u);
    std::vector<std::uint32_t> samples = patternedBuffer<std::uint32_t>(layout);
    const std::vector<std::uint32_t> original = samples;

    ASSERT_TRUE(an::swapImageByteOrder(execution, an::SampleType::UInt32, samples.data(), layout).ok());
    EXPECT_NE(std::memcmp(original.data(), samples.data(), original.size() * sizeof(std::uint32_t)), 0);
    ASSERT_TRUE(an::swapImageByteOrder(execution, an::SampleType::UInt32, samples.data(), layout).ok());
    EXPECT_EQ(std::memcmp(original.data(), samples.data(), original.size() * sizeof(std::uint32_t)), 0);
}

TEST(ByteOrderTest, ASwapReversesTheBytesOfEachSampleAndNothingElse)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<std::uint16_t>(2u, 1u, 1u, false, 0u);
    std::vector<std::uint16_t> samples{0x1234u, 0xABCDu};

    ASSERT_TRUE(an::swapImageByteOrder(execution, an::SampleType::UInt16, samples.data(), layout).ok());
    EXPECT_EQ(samples[0], 0x3412u);
    EXPECT_EQ(samples[1], 0xCDABu);
}

TEST(ByteOrderTest, BothBackendsSwapIdentically)
{
    an::EngineContext engine = makeEngineContext();
    const an::ImageLayout layout = layoutOf<double>(23u, 4u, 1u, false, 1u);
    const std::vector<double> original = patternedBuffer<double>(layout);

    std::vector<double> onReference = original;
    {
        an::ExecutionContext reference = makeExecution(engine, an::ComputeBackend::Reference);
        ASSERT_TRUE(an::swapImageByteOrder(reference, an::SampleType::Float64, onReference.data(), layout).ok());
    }

    std::vector<double> onCpu = original;
    {
        an::ExecutionContext compiled = makeExecution(engine, an::ComputeBackend::Cpu);
        ASSERT_TRUE(an::swapImageByteOrder(compiled, an::SampleType::Float64, onCpu.data(), layout).ok());
    }

    EXPECT_EQ(std::memcmp(onReference.data(), onCpu.data(), original.size() * sizeof(double)), 0);
}

TEST(ByteOrderTest, AOneByteRepresentationHasNothingToSwap)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<std::uint8_t>(4u, 2u, 1u, false, 0u);
    std::vector<std::uint8_t> samples(64u, 7u);

    const an::Status refused = an::swapImageByteOrder(execution, an::SampleType::UInt8, samples.data(), layout);
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(samples[0], 7u);
}

TEST(ByteOrderTest, TheTypedSpellingReachesTheSameCode)
{
    an::EngineContext engine = makeEngineContext();
    an::ExecutionContext execution = makeExecution(engine);
    const an::ImageLayout layout = layoutOf<std::uint16_t>(2u, 1u, 1u, false, 0u);
    std::vector<std::uint16_t> samples{0x1234u, 0xABCDu};
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{samples}, layout);
    ASSERT_TRUE(view.has_value());

    ASSERT_TRUE(an::swapByteOrder<std::uint16_t>(execution, *view).ok());
    EXPECT_EQ(samples[0], 0x3412u);
}
