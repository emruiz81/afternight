// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the Algorithms/Kernel scalar family headers.
//
// These are scalar-oracle tests, so their fixtures are deliberately awkward rather than large:
// odd row tails, padding that must not be read, explicit masks, value-derived validity, border
// coordinates, duplicate scatter destinations, and caller-owned scratch. Optimized backends will
// be compared against these exact answers.

#include <AfterNightEngine/Algorithms/Kernel/GatherScatterKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/PointwiseKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ReductionKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/ResampleKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/StencilKernel.h>
#include <AfterNightEngine/Algorithms/Kernel/TransferKernel.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/PlaneView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ExecutionPolicy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace
{

[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine,
                                                 std::stop_token stopToken = std::stop_token{})
{
    an::ExecutionOptions options;
    options.backend = an::ComputeBackend::Reference;
    options.stopToken = std::move(stopToken);
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue());
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channels = 1u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return extent.value();
}

template <typename SampleT>
[[nodiscard]] an::PlaneView<SampleT> planeOf(std::span<SampleT> samples, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t rowStrideSamples)
{
    const std::optional<an::PlaneView<SampleT>> view = an::PlaneView<SampleT>::create(
        samples, width, height, 1u, static_cast<std::uint64_t>(rowStrideSamples) * sizeof(SampleT));
    EXPECT_TRUE(view.has_value());
    return view.value();
}

[[nodiscard]] an::ValidityDescriptor validityRule(an::ValidityRule rule)
{
    an::ValidityDescriptorRequest request;
    request.rule = rule;
    const std::optional<an::ValidityDescriptor> made = an::ValidityDescriptor::create(request);
    EXPECT_TRUE(made.has_value());
    return made.value();
}

TEST(ReferenceTransferKernelTest, OddPaddedRowsAndAnExplicitMaskAreWalkedWithoutTouchingPadding)
{
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 2u;
    constexpr std::uint32_t kStride = 16u;
    std::array<float, kStride * kHeight> source{};
    std::array<float, kStride * kHeight> destination{};
    destination.fill(-99.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            source[y * kStride + x] = static_cast<float>(y * 100u + x);
        }
        source[y * kStride + kWidth] = 9000.0f;
    }

    std::array<std::uint8_t, 4u> maskBits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writableMask =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{maskBits}, extentOf(kWidth, kHeight));
    ASSERT_TRUE(writableMask.has_value());
    writableMask->fill(true);
    EXPECT_TRUE(writableMask->setValid(0u, 3u, 0u, false));
    EXPECT_TRUE(writableMask->setValid(0u, 12u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writableMask->constView();
    const an::KernelValidityView validity{.descriptor = validityRule(an::ValidityRule::ExplicitMask), .mask = &mask};

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::KernelRunResult> copied =
        an::referenceTransferKernel(execution, planeOf<const float>(source, kWidth, kHeight, kStride),
                                    planeOf<float>(destination, kWidth, kHeight, kStride), validity);
    ASSERT_TRUE(copied.hasValue()) << copied.status().message();
    EXPECT_EQ(copied->selection, (an::ValidityCount{.valid = 24u, .total = 26u}));
    EXPECT_EQ(copied->outputsWritten, 24u);
    EXPECT_EQ(destination[2], source[2]);
    EXPECT_EQ(destination[3], -99.0f);
    EXPECT_EQ(destination[kStride + 11u], source[kStride + 11u]);
    EXPECT_EQ(destination[kStride + 12u], -99.0f);
    EXPECT_EQ(destination[kWidth], -99.0f);
    EXPECT_EQ(destination[kStride + kWidth], -99.0f);
}

TEST(ReferencePointwiseKernelTest, ExactInPlaceAffineHonoursValueDerivedValidity)
{
    std::array<float, 13u> samples{0.0f, 1.0f, 2.0f, 0.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 0.0f, 10.0f, 11.0f, 12.0f};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    an::PlaneView<float> writable = planeOf<float>(samples, 13u, 1u, 13u);
    const an::KernelValidityView validity{.descriptor = validityRule(an::ValidityRule::ZeroIsNoData)};
    const an::Result<an::KernelRunResult> transformed = an::referencePointwiseAffineKernel(
        execution, writable.constView(), writable, an::AffineKernelParameters{.scale = 2.0, .bias = 1.0}, validity);
    ASSERT_TRUE(transformed.hasValue()) << transformed.status().message();
    EXPECT_EQ(transformed->selection, (an::ValidityCount{.valid = 10u, .total = 13u}));
    EXPECT_EQ(transformed->outputsWritten, 10u);
    EXPECT_EQ(samples[0], 0.0f);
    EXPECT_EQ(samples[1], 3.0f);
    EXPECT_EQ(samples[12], 25.0f);
}

TEST(ReferenceReductionKernelTest, SumCountsAndSkipsMaskedTailSamples)
{
    std::array<float, 13u> samples{};
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(index + 1u);
    }
    std::array<std::uint8_t, 2u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(13u, 1u));
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    EXPECT_TRUE(writable->setValid(0u, 0u, 0u, false));
    EXPECT_TRUE(writable->setValid(0u, 12u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::ReductionKernelResult> reduced = an::referenceSumKernel(
        execution, planeOf<const float>(samples, 13u, 1u, 13u),
        an::KernelValidityView{.descriptor = validityRule(an::ValidityRule::ExplicitMask), .mask = &mask});
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_DOUBLE_EQ(reduced->sum, 77.0);
    EXPECT_EQ(reduced->run.selection, (an::ValidityCount{.valid = 11u, .total = 13u}));
}

TEST(ReferenceHistogramOrderKernelTest, HistogramAndOrderShareTheSameFiniteSelection)
{
    const std::array<float, 9u> samples{
        -1.0f, 0.0f, 0.24f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(samples, 9u, 1u, 9u);
    const an::KernelValidityView validity{.descriptor = validityRule(an::ValidityRule::FiniteOnly)};

    std::array<std::uint64_t, 4u> bins{};
    const an::Result<an::HistogramKernelResult> histogram = an::referenceHistogramKernel(
        execution, source, bins, an::HistogramKernelParameters{.lowest = 0.0, .highest = 1.0}, validity);
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{2u, 1u, 1u, 2u}));
    EXPECT_EQ(histogram->belowRange, 1u);
    EXPECT_EQ(histogram->aboveRange, 1u);
    EXPECT_EQ(histogram->notANumber, 0u);
    EXPECT_EQ(histogram->run.selection, (an::ValidityCount{.valid = 8u, .total = 9u}));

    std::array<float, 9u> scratch{};
    const an::Result<an::OrderStatisticKernelResult> ordered =
        an::referenceOrderStatisticKernel(execution, source, 4u, scratch, validity);
    ASSERT_TRUE(ordered.hasValue()) << ordered.status().message();
    EXPECT_FLOAT_EQ(ordered->value, 0.5f);
    EXPECT_EQ(ordered->run.selection, histogram->run.selection);
    EXPECT_EQ(ordered->scratchUsed, 8u);

    bins.fill(99u);
    const an::Result<an::HistogramKernelResult> allValidHistogram = an::referenceHistogramKernel(
        execution, source, bins, an::HistogramKernelParameters{.lowest = 0.0, .highest = 1.0}, {});
    ASSERT_TRUE(allValidHistogram.hasValue()) << allValidHistogram.status().message();
    EXPECT_EQ(bins, (std::array<std::uint64_t, 4u>{2u, 1u, 1u, 2u}));
    EXPECT_EQ(allValidHistogram->run.selection.valid, 9u);
    EXPECT_EQ(allValidHistogram->notANumber, 1u);
}

TEST(ReferenceHistogramOrderKernelTest, InfinitiesAndTheDocumentedFloatTotalOrderHavePinnedAnswers)
{
    const float firstNan = std::bit_cast<float>(std::uint32_t{0x7FC00001u});
    const float secondNan = std::bit_cast<float>(std::uint32_t{0xFFC00002u});
    const std::array<float, 7u> samples{-std::numeric_limits<float>::infinity(), -0.0f,    +0.0f,    1.0f,
                                        std::numeric_limits<float>::infinity(),  firstNan, secondNan};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(samples, 7u, 1u, 7u);

    std::array<std::uint64_t, 2u> bins{};
    const an::Result<an::HistogramKernelResult> histogram = an::referenceHistogramKernel(
        execution, source, bins, an::HistogramKernelParameters{.lowest = 0.0, .highest = 1.0}, {});
    ASSERT_TRUE(histogram.hasValue()) << histogram.status().message();
    EXPECT_EQ(bins, (std::array<std::uint64_t, 2u>{2u, 1u}));
    EXPECT_EQ(histogram->belowRange, 1u);
    EXPECT_EQ(histogram->aboveRange, 1u);
    EXPECT_EQ(histogram->notANumber, 2u);
    EXPECT_EQ(histogram->run.selection, (an::ValidityCount{.valid = 7u, .total = 7u}));

    std::array<float, 7u> scratch{};
    const an::Result<an::OrderStatisticKernelResult> ordered =
        an::referenceOrderStatisticKernel(execution, source, 5u, scratch, {});
    ASSERT_TRUE(ordered.hasValue()) << ordered.status().message();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[0]), 0xFF800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[1]), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[2]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[3]), 0x3F800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[4]), 0x7F800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[5]), 0x7FC00001u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scratch[6]), 0xFFC00002u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ordered->value), 0x7FC00001u);
}

TEST(ReferenceStencilKernelTest, ConstantBordersAndAMaskedCenterHaveDefinedScalarAnswers)
{
    constexpr std::uint32_t kWidth = 5u;
    constexpr std::uint32_t kHeight = 3u;
    constexpr std::uint32_t kStride = 7u;
    std::array<float, kStride * kHeight> source{};
    std::array<float, kStride * kHeight> destination{};
    destination.fill(-1.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            source[y * kStride + x] = 1.0f;
        }
    }
    std::array<std::uint8_t, 3u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(kWidth, kHeight));
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    EXPECT_TRUE(writable->setValid(0u, 2u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();

    an::Stencil3x3KernelParameters parameters;
    parameters.coefficients.fill(1.0);
    parameters.border = an::KernelBorderMode::Constant;
    parameters.constantBorder = 2.0f;
    parameters.invalidNeighbor = 0.0f;

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::Result<an::KernelRunResult> filtered = an::referenceStencil3x3Kernel(
        execution, planeOf<const float>(source, kWidth, kHeight, kStride),
        planeOf<float>(destination, kWidth, kHeight, kStride), parameters,
        an::KernelValidityView{.descriptor = validityRule(an::ValidityRule::ExplicitMask), .mask = &mask});
    ASSERT_TRUE(filtered.hasValue()) << filtered.status().message();
    EXPECT_EQ(filtered->selection, (an::ValidityCount{.valid = 14u, .total = 15u}));
    EXPECT_FLOAT_EQ(destination[0], 14.0f);
    EXPECT_FLOAT_EQ(destination[1u * kStride + 1u], 8.0f);
    EXPECT_FLOAT_EQ(destination[1u * kStride + 2u], -1.0f);
    EXPECT_EQ(destination[kWidth], -1.0f);
}

TEST(ReferenceResampleGatherKernelTest, PixelCenterCoordinatesCoverClampConstantAndMaskPaths)
{
    const std::array<float, 12u> sourceSamples{0.0f,  1.0f,  2.0f,  3.0f,  10.0f, 11.0f,
                                               12.0f, 13.0f, 20.0f, 21.0f, 22.0f, 23.0f};
    const std::array<an::KernelCoordinate, 5u> coordinates{
        {{0.0, 0.0}, {1.49, 1.49}, {2.0, 1.0}, {-2.0, 0.0}, {9.0, 9.0}}};
    std::array<std::uint8_t, 3u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(4u, 3u));
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    EXPECT_TRUE(writable->setValid(0u, 2u, 1u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const an::KernelValidityView validity{.descriptor = validityRule(an::ValidityRule::ExplicitMask), .mask = &mask};

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(sourceSamples, 4u, 3u, 4u);
    const an::NearestKernelParameters parameters{.border = an::KernelBorderMode::Constant, .constantBorder = -5.0f};

    std::array<float, 5u> gathered{};
    gathered.fill(-99.0f);
    const an::Result<an::KernelRunResult> gather =
        an::referenceGatherKernel(execution, source, coordinates, gathered, parameters, validity);
    ASSERT_TRUE(gather.hasValue()) << gather.status().message();
    EXPECT_EQ(gathered, (std::array<float, 5u>{0.0f, 11.0f, -99.0f, -5.0f, -5.0f}));
    EXPECT_EQ(gather->selection, (an::ValidityCount{.valid = 4u, .total = 5u}));

    std::array<float, 7u> resampled{};
    resampled.fill(-99.0f);
    const an::Result<an::KernelRunResult> resample = an::referenceResampleNearestKernel(
        execution, source, planeOf<float>(resampled, 5u, 1u, 7u), coordinates, parameters, validity);
    ASSERT_TRUE(resample.hasValue()) << resample.status().message();
    EXPECT_TRUE(std::ranges::equal(std::span<const float>{resampled}.first<5>(), std::span<const float>{gathered}));
    EXPECT_EQ(resampled[5], -99.0f);
    EXPECT_EQ(resampled[6], -99.0f);
}

TEST(ReferenceResampleGatherKernelTest, ClampAndStencilEdgesAreDefinedAndCoordinatesValidateBeforeWriting)
{
    const std::array<float, 6u> sourceSamples{0.0f, 1.0f, 2.0f, 10.0f, 11.0f, 12.0f};
    const std::array<an::KernelCoordinate, 3u> coordinates{{{-4.0, -3.0}, {0.5, 0.5}, {8.0, 9.0}}};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(sourceSamples, 3u, 2u, 3u);

    std::array<float, 3u> gathered{};
    const an::Result<an::KernelRunResult> gather =
        an::referenceGatherKernel(execution, source, coordinates, gathered,
                                  an::NearestKernelParameters{.border = an::KernelBorderMode::Clamp}, {});
    ASSERT_TRUE(gather.hasValue()) << gather.status().message();
    EXPECT_EQ(gathered, (std::array<float, 3u>{0.0f, 11.0f, 12.0f}));

    std::array<float, 3u> resampled{};
    const an::Result<an::KernelRunResult> resample =
        an::referenceResampleNearestKernel(execution, source, planeOf<float>(resampled, 3u, 1u, 3u), coordinates,
                                           an::NearestKernelParameters{.border = an::KernelBorderMode::Clamp}, {});
    ASSERT_TRUE(resample.hasValue()) << resample.status().message();
    EXPECT_EQ(resampled, gathered);

    std::array<float, 4u> ones{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4u> stencilOutput{};
    an::Stencil3x3KernelParameters stencilParameters;
    stencilParameters.coefficients.fill(1.0);
    const an::Result<an::KernelRunResult> stencil =
        an::referenceStencil3x3Kernel(execution, planeOf<const float>(ones, 2u, 2u, 2u),
                                      planeOf<float>(stencilOutput, 2u, 2u, 2u), stencilParameters, {});
    ASSERT_TRUE(stencil.hasValue()) << stencil.status().message();
    EXPECT_EQ(stencilOutput, (std::array<float, 4u>{9.0f, 9.0f, 9.0f, 9.0f}));

    stencilParameters.border = an::KernelBorderMode::Reflect;
    stencilOutput.fill(7.0f);
    EXPECT_EQ(an::referenceStencil3x3Kernel(execution, planeOf<const float>(ones, 2u, 2u, 2u),
                                            planeOf<float>(stencilOutput, 2u, 2u, 2u), stencilParameters, {})
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(stencilOutput, (std::array<float, 4u>{7.0f, 7.0f, 7.0f, 7.0f}));

    std::array<float, 2u> untouched{7.0f, 7.0f};
    const std::array<an::KernelCoordinate, 2u> invalidCoordinates{
        {{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 0.0}}};
    const an::Result<an::KernelRunResult> invalid =
        an::referenceGatherKernel(execution, source, invalidCoordinates, untouched,
                                  an::NearestKernelParameters{.border = an::KernelBorderMode::Clamp}, {});
    EXPECT_EQ(invalid.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(untouched, (std::array<float, 2u>{7.0f, 7.0f}));
}

TEST(ReferenceScatterKernelTest, ConflictsAreDeterministicAndRejectPreflightsBeforeWriting)
{
    const std::array<float, 5u> sourceSamples{1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
    const std::array<an::KernelCoordinate, 5u> coordinates{
        {{1.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {9.0, 0.0}, {3.0, 0.0}}};
    std::array<std::uint8_t, 1u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(5u, 1u));
    ASSERT_TRUE(writable.has_value());
    writable->fill(true);
    EXPECT_TRUE(writable->setValid(0u, 4u, 0u, false));
    const an::ValidityMaskView<const std::uint8_t> mask = writable->constView();
    const an::KernelValidityView validity{.descriptor = validityRule(an::ValidityRule::ExplicitMask), .mask = &mask};

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(sourceSamples, 5u, 1u, 5u);

    std::array<float, 4u> rejectedDestination{};
    rejectedDestination.fill(20.0f);
    const an::Result<an::ScatterKernelResult> rejected =
        an::referenceScatterKernel(execution, source, coordinates, planeOf<float>(rejectedDestination, 4u, 1u, 4u),
                                   an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::Reject,
                                                               .bounds = an::ScatterBoundsPolicy::Discard},
                                   validity);
    ASSERT_FALSE(rejected.hasValue());
    EXPECT_EQ(rejected.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(rejectedDestination, (std::array<float, 4u>{20.0f, 20.0f, 20.0f, 20.0f}));

    std::array<float, 4u> summedDestination{};
    const an::Result<an::ScatterKernelResult> summed =
        an::referenceScatterKernel(execution, source, coordinates, planeOf<float>(summedDestination, 4u, 1u, 4u),
                                   an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::Add,
                                                               .bounds = an::ScatterBoundsPolicy::Discard},
                                   validity);
    ASSERT_TRUE(summed.hasValue()) << summed.status().message();
    EXPECT_EQ(summedDestination, (std::array<float, 4u>{0.0f, 3.0f, 4.0f, 0.0f}));
    EXPECT_EQ(summed->run.selection, (an::ValidityCount{.valid = 4u, .total = 5u}));
    EXPECT_EQ(summed->run.outputsWritten, 3u);
    EXPECT_EQ(summed->conflicts, 1u);
    EXPECT_EQ(summed->discarded, 1u);
}

TEST(ReferenceScatterKernelTest, FirstLastAndRejectingBoundsUseFixedSourceOrder)
{
    const std::array<float, 3u> sourceSamples{1.0f, 2.0f, 4.0f};
    const std::array<an::KernelCoordinate, 3u> coordinates{{{0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}}};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<const float> source = planeOf<const float>(sourceSamples, 3u, 1u, 3u);

    std::array<float, 2u> firstOutput{};
    const an::Result<an::ScatterKernelResult> first =
        an::referenceScatterKernel(execution, source, coordinates, planeOf<float>(firstOutput, 2u, 1u, 2u),
                                   an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::First,
                                                               .bounds = an::ScatterBoundsPolicy::Reject},
                                   {});
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(firstOutput, (std::array<float, 2u>{1.0f, 4.0f}));
    EXPECT_EQ(first->conflicts, 1u);
    EXPECT_EQ(first->run.outputsWritten, 2u);

    std::array<float, 2u> lastOutput{};
    const an::Result<an::ScatterKernelResult> last =
        an::referenceScatterKernel(execution, source, coordinates, planeOf<float>(lastOutput, 2u, 1u, 2u),
                                   an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::Last,
                                                               .bounds = an::ScatterBoundsPolicy::Reject},
                                   {});
    ASSERT_TRUE(last.hasValue()) << last.status().message();
    EXPECT_EQ(lastOutput, (std::array<float, 2u>{2.0f, 4.0f}));
    EXPECT_EQ(last->conflicts, 1u);
    EXPECT_EQ(last->run.outputsWritten, 3u);

    std::array<float, 2u> rejectedOutput{9.0f, 9.0f};
    const std::array<an::KernelCoordinate, 3u> outOfBounds{{{0.0, 0.0}, {7.0, 0.0}, {1.0, 0.0}}};
    const an::Result<an::ScatterKernelResult> rejected =
        an::referenceScatterKernel(execution, source, outOfBounds, planeOf<float>(rejectedOutput, 2u, 1u, 2u),
                                   an::ScatterKernelParameters{.conflicts = an::ScatterConflictPolicy::Last,
                                                               .bounds = an::ScatterBoundsPolicy::Reject},
                                   {});
    EXPECT_EQ(rejected.code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(rejectedOutput, (std::array<float, 2u>{9.0f, 9.0f}));
}

TEST(ReferenceKernelValidationTest, InvalidExecutionShapesMasksAliasingAndScratchAreReturnedFailures)
{
    std::array<float, 16u> samples{};
    std::array<float, 16u> output{};
    const an::PlaneView<const float> source = planeOf<const float>(samples, 4u, 4u, 4u);
    const an::PlaneView<float> destination = planeOf<float>(output, 4u, 4u, 4u);
    an::ExecutionContext emptyExecution;
    EXPECT_EQ(an::referenceTransferKernel(emptyExecution, source, destination, {}).code(),
              an::ErrorCode::NotInitialized);

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::PlaneView<float> aliased = planeOf<float>(samples, 4u, 4u, 4u);
    EXPECT_EQ(an::referenceStencil3x3Kernel(execution, aliased.constView(), aliased, {}, {}).code(),
              an::ErrorCode::InvalidArgument);

    std::array<std::uint8_t, 2u> mismatchedMaskBits{};
    const std::optional<an::ValidityMaskView<const std::uint8_t>> mismatchedMask =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>{mismatchedMaskBits},
                                                              extentOf(4u, 2u));
    ASSERT_TRUE(mismatchedMask.has_value());
    EXPECT_EQ(an::referenceSumKernel(execution, source,
                                     an::KernelValidityView{.descriptor = validityRule(an::ValidityRule::ExplicitMask),
                                                            .mask = &*mismatchedMask})
                  .code(),
              an::ErrorCode::IncompatibleArguments);

    std::array<float, 15u> shortScratch{};
    EXPECT_EQ(an::referenceOrderStatisticKernel(execution, source, 0u, shortScratch, {}).code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(ReferenceKernelValidationTest, EmptySelectionsAndPartialOverlapAreRefusedWithoutAResultWrite)
{
    std::array<float, 6u> samples{};
    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine);
    const an::KernelValidityView noZero{.descriptor = validityRule(an::ValidityRule::ZeroIsNoData)};
    const an::PlaneView<const float> emptySource = planeOf<const float>(samples, 3u, 2u, 3u);

    EXPECT_EQ(an::referenceSumKernel(execution, emptySource, noZero).code(), an::ErrorCode::EmptySelection);
    std::array<std::uint64_t, 2u> bins{9u, 9u};
    EXPECT_EQ(an::referenceHistogramKernel(execution, emptySource, bins, {}, noZero).code(),
              an::ErrorCode::EmptySelection);
    EXPECT_EQ(bins, (std::array<std::uint64_t, 2u>{0u, 0u}));
    std::array<float, 6u> scratch{};
    EXPECT_EQ(an::referenceOrderStatisticKernel(execution, emptySource, 0u, scratch, noZero).code(),
              an::ErrorCode::EmptySelection);

    std::array<float, 8u> overlapping{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    const std::array<float, 8u> before = overlapping;
    const an::PlaneView<const float> source =
        planeOf<const float>(std::span<const float>{overlapping}.first<4>(), 4u, 1u, 4u);
    const an::PlaneView<float> shifted = planeOf<float>(std::span<float>{overlapping}.subspan(1u, 4u), 4u, 1u, 4u);
    EXPECT_EQ(an::referenceTransferKernel(execution, source, shifted, {}).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(overlapping, before);

    alignas(std::uint64_t) std::array<float, 4u> sharedHistogram{4.0f, 3.0f, 2.0f, 1.0f};
    const std::array<float, 4u> histogramBefore = sharedHistogram;
    const an::PlaneView<const float> histogramSource = planeOf<const float>(sharedHistogram, 4u, 1u, 4u);
    std::span<std::uint64_t> overlappingBins{reinterpret_cast<std::uint64_t*>(sharedHistogram.data()), 2u};
    EXPECT_EQ(an::referenceHistogramKernel(execution, histogramSource, overlappingBins, {}, {}).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(sharedHistogram, histogramBefore);

    std::array<float, 4u> sharedOrder{4.0f, 3.0f, 2.0f, 1.0f};
    const std::array<float, 4u> orderBefore = sharedOrder;
    const an::PlaneView<const float> orderSource = planeOf<const float>(sharedOrder, 4u, 1u, 4u);
    EXPECT_EQ(an::referenceOrderStatisticKernel(execution, orderSource, 0u, sharedOrder, {}).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(sharedOrder, orderBefore);
}

TEST(ReferenceKernelCancellationTest, ARequestedStopIsObservedBeforeAnyOutputIsWritten)
{
    std::array<float, 13u> source{};
    std::array<float, 13u> destination{};
    destination.fill(7.0f);
    std::stop_source stop;
    stop.request_stop();

    an::EngineContext engine = makeEngine();
    an::ExecutionContext execution = makeExecution(engine, stop.get_token());
    const an::Result<an::KernelRunResult> result = an::referencePointwiseAffineKernel(
        execution, planeOf<const float>(source, 13u, 1u, 13u), planeOf<float>(destination, 13u, 1u, 13u), {}, {});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.code(), an::ErrorCode::Cancelled);
    for (const float sample : destination)
    {
        EXPECT_EQ(sample, 7.0f);
    }
}

} // namespace
