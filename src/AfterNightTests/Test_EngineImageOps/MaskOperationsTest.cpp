// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for building, combining, counting and applying packed validity masks.

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency = 3u)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

[[nodiscard]] an::ImageLayout planarLayout(an::SampleType type, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t channels, std::uint32_t rowPaddingSamples = 0u)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t elementBytes = an::sampleTypeSizeBytes(type);
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = elementBytes,
                                         .samplesPerPixel = 1u,
                                         .planeCount = channels,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = static_cast<std::uint64_t>(width + rowPaddingSamples) * elementBytes;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * height, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] an::ImageExtent maskExtent(std::uint32_t width, std::uint32_t height, std::uint32_t planes)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, planes, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return *extent;
}

/// A packed mask over caller-owned bytes, with the pad bits of every row already at zero.
struct OwnedMask
{
    /// The bytes the mask lives in.
    std::vector<std::uint8_t> bytes;

    /// The mask's dimensions.
    an::ImageExtent extent;
};

[[nodiscard]] OwnedMask ownedMask(std::uint32_t width, std::uint32_t height, std::uint32_t planes,
                                  std::uint8_t fill = 0u)
{
    const an::ImageExtent extent = maskExtent(width, height, planes);
    const std::size_t rowBytes = static_cast<std::size_t>(an::packedMaskRowBytes(width));
    return OwnedMask{std::vector<std::uint8_t>(rowBytes * height * planes, fill), extent};
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

[[nodiscard]] an::MaskThresholdRequest above(double bound)
{
    an::MaskThresholdRequest request;
    request.threshold.predicate = an::MaskPredicate::AtOrAbove;
    request.threshold.lowerBound = bound;
    return request;
}

} // namespace

TEST(MaskOperationsTest, AThresholdSelectsWhatItSaysAndLeavesThePadBitsAtZero)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // A width that is not a multiple of eight, so every row has spare bits in its last byte.
    constexpr std::uint32_t kWidth = 11u;
    constexpr std::uint32_t kHeight = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u, 3u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            // Exact binary fractions, so that a bound stated as a double and a sample held as a
            // float compare the way the arithmetic says rather than the way a decimal looks.
            samples[static_cast<std::size_t>(y) * (kWidth + 3u) + x] = static_cast<float>(x) / 16.0f;
        }
    }

    struct Case
    {
        an::MaskPredicate predicate;
        double lower;
        std::optional<double> upper;
        std::uint64_t expected;
    };
    const std::array<Case, 6u> cases{
        Case{an::MaskPredicate::Below, 0.25, std::nullopt, 4u * kHeight},
        Case{an::MaskPredicate::AtOrBelow, 0.25, std::nullopt, 5u * kHeight},
        Case{an::MaskPredicate::AtOrAbove, 0.25, std::nullopt, 7u * kHeight},
        Case{an::MaskPredicate::Above, 0.25, std::nullopt, 6u * kHeight},
        Case{an::MaskPredicate::InsideClosedRange, 0.125, 0.375, 5u * kHeight},
        Case{an::MaskPredicate::OutsideClosedRange, 0.125, 0.375, 6u * kHeight},
    };

    for (const Case& example : cases)
    {
        OwnedMask mask = ownedMask(kWidth, kHeight, 1u, 0xFFu);
        an::MaskThresholdRequest request;
        request.threshold.predicate = example.predicate;
        request.threshold.lowerBound = example.lower;
        request.threshold.upperBound = example.upper;

        const an::Status built =
            an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, request, viewOf(mask));
        ASSERT_TRUE(built.ok()) << an::maskPredicateName(example.predicate) << ": " << built.message();

        const an::ValidityMaskView<const std::uint8_t> view = constViewOf(mask);
        EXPECT_EQ(view.countValid(), example.expected) << an::maskPredicateName(example.predicate);

        // The three spare bits of every row's second byte are zero, whatever the buffer held before.
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::span<const std::uint8_t> row = *view.row(0u, y);
            EXPECT_EQ(row.back() & static_cast<std::uint8_t>(~an::packedMaskLastByteMask(kWidth)), 0u);
        }
    }

    // Every representation compares the same way, because the bound is compared in double.
    const an::ImageLayout integerLayout = planarLayout(an::SampleType::UInt16, kWidth, kHeight, 1u);
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(integerLayout.totalBytes() / sizeof(std::uint16_t)), 0u);
    for (std::size_t index = 0u; index < codes.size(); ++index)
    {
        codes[index] = static_cast<std::uint16_t>(index % kWidth);
    }
    OwnedMask codeMask = ownedMask(kWidth, kHeight, 1u);
    const an::Status codeBuilt = an::buildImageMask(execution, an::SampleType::UInt16, codes.data(), integerLayout,
                                                    above(5.0), viewOf(codeMask));
    ASSERT_TRUE(codeBuilt.ok()) << codeBuilt.message();
    EXPECT_EQ(constViewOf(codeMask).countValid(), 6u * kHeight);
}

TEST(MaskOperationsTest, ANonFiniteSampleDoesWhatTheRequestSaysRatherThanWhatComparisonDoes)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 4u, 1u, 1u);
    std::vector<float> samples{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), 0.75f};

    OwnedMask rejected = ownedMask(4u, 1u, 1u);
    an::MaskThresholdRequest request = above(0.5);
    request.threshold.nonFiniteSelected = false;
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, request, viewOf(rejected)).ok());
    EXPECT_EQ(constViewOf(rejected).countValid(), 1u);

    OwnedMask kept = ownedMask(4u, 1u, 1u);
    request.threshold.nonFiniteSelected = true;
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, request, viewOf(kept)).ok());
    EXPECT_EQ(constViewOf(kept).countValid(), 4u);

    // A comparison would have put positive infinity above the bound and left the two others below
    // it; stating the answer is what makes both readings available.
    EXPECT_TRUE(constViewOf(kept).test(0u, 0u, 0u).value_or(false));
    EXPECT_FALSE(constViewOf(rejected).test(0u, 1u, 0u).value_or(true));
}

TEST(MaskOperationsTest, ASharedMaskNeedsAChannelRuleAndAPerChannelMaskRefusesOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 4u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, 1u, 3u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
    // Column zero passes in no channel, one in the first only, two in two, three in all three.
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            samples[static_cast<std::size_t>(channel) * kWidth + x] = x > channel ? 1.0f : 0.0f;
        }
    }

    OwnedMask everyChannel = ownedMask(kWidth, 1u, 1u);
    an::MaskThresholdRequest request = above(0.5);
    request.channelRule = an::MaskChannelRule::EveryChannel;
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, request, viewOf(everyChannel))
            .ok());
    EXPECT_EQ(constViewOf(everyChannel).countValid(), 1u);
    EXPECT_TRUE(constViewOf(everyChannel).test(0u, 3u, 0u).value_or(false));

    OwnedMask anyChannel = ownedMask(kWidth, 1u, 1u);
    request.channelRule = an::MaskChannelRule::AnyChannel;
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, request, viewOf(anyChannel))
            .ok());
    EXPECT_EQ(constViewOf(anyChannel).countValid(), 3u);

    // Without a rule the shared mask cannot be written at all.
    OwnedMask unruled = ownedMask(kWidth, 1u, 1u);
    EXPECT_EQ(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, above(0.5), viewOf(unruled))
            .code(),
        an::ErrorCode::InvalidArgument);

    // A mask with one plane per channel decides each on its own and has nothing to combine.
    OwnedMask perChannel = ownedMask(kWidth, 1u, 3u);
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, above(0.5), viewOf(perChannel))
            .ok());
    EXPECT_EQ(constViewOf(perChannel).countValid(), 3u + 2u + 1u);

    an::MaskThresholdRequest ruled = above(0.5);
    ruled.channelRule = an::MaskChannelRule::EveryChannel;
    OwnedMask refused = ownedMask(kWidth, 1u, 3u);
    EXPECT_EQ(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, ruled, viewOf(refused)).code(),
        an::ErrorCode::InvalidArgument);
}

TEST(MaskOperationsTest, MarkingUsesTheValidityRuleAndRefusesTheRuleThatNeedsAMask)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, 5u, 1u, 1u);
    std::vector<float> samples{0.0f, 1.5f, std::numeric_limits<float>::quiet_NaN(), -2.0f, 0.5f};

    OwnedMask finite = ownedMask(5u, 1u, 1u);
    const an::ValidityDescriptor finiteOnly =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    ASSERT_TRUE(
        an::markImageValidity(execution, an::SampleType::Float32, samples.data(), layout, finiteOnly, viewOf(finite))
            .ok());
    EXPECT_EQ(constViewOf(finite).countValid(), 4u);

    OwnedMask nonZero = ownedMask(5u, 1u, 1u);
    const an::ValidityDescriptor zeroIsNoData =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(
        an::markImageValidity(execution, an::SampleType::Float32, samples.data(), layout, zeroIsNoData, viewOf(nonZero))
            .ok());
    EXPECT_EQ(constViewOf(nonZero).countValid(), 4u);

    OwnedMask ranged = ownedMask(5u, 1u, 1u);
    const an::ValidityDescriptor domainRange = *an::ValidityDescriptor::create(
        an::ValidityDescriptorRequest{.rule = an::ValidityRule::DomainRange, .lowestValid = 0.0, .highestValid = 1.0});
    ASSERT_TRUE(
        an::markImageValidity(execution, an::SampleType::Float32, samples.data(), layout, domainRange, viewOf(ranged))
            .ok());
    EXPECT_EQ(constViewOf(ranged).countValid(), 2u);

    OwnedMask refused = ownedMask(5u, 1u, 1u);
    const an::ValidityDescriptor explicitMask =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    EXPECT_EQ(
        an::markImageValidity(execution, an::SampleType::Float32, samples.data(), layout, explicitMask, viewOf(refused))
            .code(),
        an::ErrorCode::InvalidArgument);
}

TEST(MaskOperationsTest, CombineInvertAndCountAgreeWithTheImageModelAndLeaveThePaddingAlone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 37u;
    constexpr std::uint32_t kHeight = 9u;
    OwnedMask left = ownedMask(kWidth, kHeight, 2u);
    OwnedMask right = ownedMask(kWidth, kHeight, 2u);
    for (std::uint32_t plane = 0u; plane < 2u; ++plane)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kWidth; ++x)
            {
                EXPECT_TRUE(viewOf(left).setValid(plane, x, y, (x + y) % 3u != 0u));
                EXPECT_TRUE(viewOf(right).setValid(plane, x, y, (x * 2u + plane) % 5u != 0u));
            }
        }
    }

    for (const an::ValidityCombination combination :
         {an::ValidityCombination::Intersection, an::ValidityCombination::Union})
    {
        OwnedMask operationAnswer = ownedMask(kWidth, kHeight, 2u, 0xAAu);
        OwnedMask modelAnswer = ownedMask(kWidth, kHeight, 2u, 0xAAu);
        ASSERT_TRUE(an::combineImageMasks(execution, viewOf(operationAnswer), constViewOf(left), constViewOf(right),
                                          combination)
                        .ok());
        ASSERT_TRUE(an::combineValidity(viewOf(modelAnswer), constViewOf(left), constViewOf(right), combination).ok());
        EXPECT_EQ(operationAnswer.bytes, modelAnswer.bytes) << an::validityCombinationName(combination);

        const an::Result<an::ValidityCount> counted = an::countImageMask(execution, constViewOf(operationAnswer));
        ASSERT_TRUE(counted.hasValue()) << counted.status().message();
        EXPECT_EQ(counted->valid, constViewOf(operationAnswer).countValid());
        EXPECT_EQ(counted->total, static_cast<std::uint64_t>(kWidth) * kHeight * 2u);
    }

    // An accumulation into one of its own sources is what a footprint over many frames does.
    OwnedMask accumulator = ownedMask(kWidth, kHeight, 2u);
    ASSERT_TRUE(an::combineImageMasks(execution, viewOf(accumulator), constViewOf(left), constViewOf(right),
                                      an::ValidityCombination::Union)
                    .ok());
    OwnedMask expectedAccumulator = accumulator;
    ASSERT_TRUE(an::combineImageMasks(execution, viewOf(accumulator), constViewOf(accumulator), constViewOf(left),
                                      an::ValidityCombination::Union)
                    .ok());
    ASSERT_TRUE(an::combineValidity(viewOf(expectedAccumulator), constViewOf(expectedAccumulator), constViewOf(left),
                                    an::ValidityCombination::Union)
                    .ok());
    EXPECT_EQ(accumulator.bytes, expectedAccumulator.bytes);

    // Inverting into a second mask and inverting in place agree with the model, and twice is the
    // identity.
    OwnedMask inverted = ownedMask(kWidth, kHeight, 2u, 0x55u);
    ASSERT_TRUE(an::invertImageMask(execution, viewOf(inverted), constViewOf(left)).ok());
    OwnedMask modelInverted = left;
    an::invertValidity(viewOf(modelInverted));
    EXPECT_EQ(inverted.bytes, modelInverted.bytes);

    OwnedMask inPlace = left;
    ASSERT_TRUE(an::invertImageMask(execution, viewOf(inPlace), constViewOf(inPlace)).ok());
    EXPECT_EQ(inPlace.bytes, modelInverted.bytes);
    ASSERT_TRUE(an::invertImageMask(execution, viewOf(inPlace), constViewOf(inPlace)).ok());
    EXPECT_EQ(inPlace.bytes, left.bytes);

    // The count does not depend on how many workers the operation was granted.
    const an::ExecutionContext single = makeExecution(engine, 1u);
    const an::Result<an::ValidityCount> oneWorker = an::countImageMask(single, constViewOf(left));
    const an::Result<an::ValidityCount> manyWorkers = an::countImageMask(execution, constViewOf(left));
    ASSERT_TRUE(oneWorker.hasValue());
    ASSERT_TRUE(manyWorkers.hasValue());
    EXPECT_EQ(*oneWorker, *manyWorkers);

    // Shapes that disagree, and an operation that holds nothing, are refused by name.
    OwnedMask narrower = ownedMask(kWidth - 1u, kHeight, 2u);
    EXPECT_EQ(an::combineImageMasks(execution, viewOf(narrower), constViewOf(left), constViewOf(right),
                                    an::ValidityCombination::Union)
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::invertImageMask(execution, viewOf(narrower), constViewOf(left)).code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::countImageMask(an::ExecutionContext{}, constViewOf(left)).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST(MaskOperationsTest, ApplyingAMaskWritesOneConvertedValueAndCountsWhatItReplaced)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 9u;
    constexpr std::uint32_t kHeight = 2u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 2u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 3.5f);

    OwnedMask shared = ownedMask(kWidth, kHeight, 1u);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_TRUE(viewOf(shared).setValid(0u, x, y, x % 2u == 0u));
        }
    }

    const an::Result<an::MaskApplyResult> applied =
        an::applyImageMask(execution, an::SampleType::Float32, samples.data(), layout, constViewOf(shared), -1.0);
    ASSERT_TRUE(applied.hasValue()) << applied.status().message();
    EXPECT_EQ(applied->replaced, 4u * kHeight * 2u);
    EXPECT_EQ(applied->total, static_cast<std::uint64_t>(kWidth) * kHeight * 2u);
    EXPECT_FALSE(applied->saturatedValue);

    // A shared plane answers for every channel of the pixel.
    for (std::uint32_t channel = 0u; channel < 2u; ++channel)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const float value = samples[static_cast<std::size_t>(channel) * kWidth * kHeight + x];
            EXPECT_FLOAT_EQ(value, x % 2u == 0u ? 3.5f : -1.0f) << "channel " << channel << " column " << x;
        }
    }

    // The written value is a conversion: into a narrow integer under the full-scale domain, one is
    // the representation's own full scale rather than the code value one.
    const an::ImageLayout codeLayout = planarLayout(an::SampleType::UInt8, kWidth, kHeight, 1u);
    std::vector<std::uint8_t> codes(static_cast<std::size_t>(codeLayout.totalBytes()), 7u);
    OwnedMask nothingValid = ownedMask(kWidth, kHeight, 1u);
    const an::Result<an::MaskApplyResult> full =
        an::applyImageMask(execution, an::SampleType::UInt8, codes.data(), codeLayout, constViewOf(nothingValid), 1.0);
    ASSERT_TRUE(full.hasValue()) << full.status().message();
    EXPECT_EQ(codes[0], 255u);

    an::ConversionPolicy codeValue;
    codeValue.domain = an::DomainMapping::CodeValue;
    std::vector<std::uint8_t> literal(static_cast<std::size_t>(codeLayout.totalBytes()), 7u);
    const an::Result<an::MaskApplyResult> asWritten = an::applyImageMask(
        execution, an::SampleType::UInt8, literal.data(), codeLayout, constViewOf(nothingValid), 1.0, codeValue);
    ASSERT_TRUE(asWritten.hasValue()) << asWritten.status().message();
    EXPECT_EQ(literal[0], 1u);

    // A value the representation cannot hold is saturated once and reported once.
    std::vector<std::uint8_t> saturating(static_cast<std::size_t>(codeLayout.totalBytes()), 7u);
    const an::Result<an::MaskApplyResult> saturated = an::applyImageMask(
        execution, an::SampleType::UInt8, saturating.data(), codeLayout, constViewOf(nothingValid), 4000.0, codeValue);
    ASSERT_TRUE(saturated.hasValue()) << saturated.status().message();
    EXPECT_TRUE(saturated->saturatedValue);
    EXPECT_EQ(saturating[0], 255u);

    // A value that is not finite, and a mask that does not cover the image, are refused.
    EXPECT_EQ(an::applyImageMask(execution, an::SampleType::Float32, samples.data(), layout, constViewOf(shared),
                                 std::numeric_limits<double>::quiet_NaN())
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    OwnedMask wrongShape = ownedMask(kWidth + 1u, kHeight, 1u);
    EXPECT_EQ(
        an::applyImageMask(execution, an::SampleType::Float32, samples.data(), layout, constViewOf(wrongShape), 0.0)
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
}

TEST(MaskOperationsTest, BinarizeWritesTwoStatedValuesAndAgreesWithTheMaskItWouldHaveBuilt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 12u;
    constexpr std::uint32_t kHeight = 3u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    std::vector<float> samples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 0.0f);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(index % 12u) / 11.0f;
    }

    std::vector<float> destination(samples.size(), -9.0f);
    an::BinarizeRequest request;
    request.threshold.predicate = an::MaskPredicate::Above;
    request.threshold.lowerBound = 0.5;
    request.selectedValue = 1.0;
    request.unselectedValue = 0.25;

    const an::Result<an::BinarizeResult> binarized = an::binarizeImage(
        execution, an::SampleType::Float32, samples.data(), layout, request, destination.data(), layout);
    ASSERT_TRUE(binarized.hasValue()) << binarized.status().message();
    EXPECT_EQ(binarized->total, static_cast<std::uint64_t>(kWidth) * kHeight);
    EXPECT_EQ(binarized->saturatedValues, 0u);

    // The same relation, built into a mask, selects the same samples.
    OwnedMask mask = ownedMask(kWidth, kHeight, 1u);
    an::MaskThresholdRequest maskRequest;
    maskRequest.threshold = request.threshold;
    ASSERT_TRUE(
        an::buildImageMask(execution, an::SampleType::Float32, samples.data(), layout, maskRequest, viewOf(mask)).ok());
    EXPECT_EQ(binarized->selected, constViewOf(mask).countValid());

    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            const bool selected = constViewOf(mask).test(0u, x, y).value_or(false);
            EXPECT_FLOAT_EQ(destination[static_cast<std::size_t>(y) * kWidth + x], selected ? 1.0f : 0.25f);
        }
    }

    // In place writes the same samples.
    std::vector<float> inPlace = samples;
    const an::Result<an::BinarizeResult> again =
        an::binarizeImage(execution, an::SampleType::Float32, inPlace.data(), layout, request, inPlace.data(), layout);
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_EQ(inPlace, destination);

    // The two written values are converted once, so a narrow integer destination saturates once.
    const an::ImageLayout codeLayout = planarLayout(an::SampleType::Int8, kWidth, kHeight, 1u);
    std::vector<std::int8_t> codes(static_cast<std::size_t>(codeLayout.totalBytes()), 0);
    std::vector<std::int8_t> written(codes.size(), 0);
    an::BinarizeRequest codeRequest;
    codeRequest.threshold.predicate = an::MaskPredicate::AtOrAbove;
    codeRequest.threshold.lowerBound = 0.0;
    codeRequest.selectedValue = 900.0;
    codeRequest.unselectedValue = -900.0;
    codeRequest.conversion.domain = an::DomainMapping::CodeValue;
    const an::Result<an::BinarizeResult> saturated = an::binarizeImage(
        execution, an::SampleType::Int8, codes.data(), codeLayout, codeRequest, written.data(), codeLayout);
    ASSERT_TRUE(saturated.hasValue()) << saturated.status().message();
    EXPECT_EQ(saturated->saturatedValues, 2u);
    EXPECT_EQ(written[0], 127);

    // Malformed requests are refused before anything is written.
    an::BinarizeRequest broken = request;
    broken.threshold.upperBound = 2.0;
    EXPECT_EQ(an::binarizeImage(execution, an::SampleType::Float32, samples.data(), layout, broken, destination.data(),
                                layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    an::BinarizeRequest rangeWithoutBound = request;
    rangeWithoutBound.threshold.predicate = an::MaskPredicate::InsideClosedRange;
    EXPECT_EQ(an::binarizeImage(execution, an::SampleType::Float32, samples.data(), layout, rangeWithoutBound,
                                destination.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    an::BinarizeRequest infiniteValue = request;
    infiniteValue.selectedValue = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::binarizeImage(execution, an::SampleType::Float32, samples.data(), layout, infiniteValue,
                                destination.data(), layout)
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);

    EXPECT_EQ(an::binarizeImage(execution, an::SampleType::Float32, samples.data(), layout, request,
                                samples.data() + 1u, layout)
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
}

TEST(MaskOperationsTest, AValidityTransitionMaterialisesTheMaskItsAnswerNeeds)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    constexpr std::uint32_t kWidth = 6u;
    constexpr std::uint32_t kHeight = 2u;
    const an::ImageLayout layout = planarLayout(an::SampleType::Float32, kWidth, kHeight, 1u);
    std::vector<float> finiteSamples(static_cast<std::size_t>(layout.totalBytes() / sizeof(float)), 1.0f);
    finiteSamples[1] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> zeroSamples(finiteSamples.size(), 2.0f);
    zeroSamples[2] = 0.0f;

    an::MaskedValidityInput finiteInput;
    finiteInput.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    finiteInput.sampleType = an::SampleType::Float32;
    finiteInput.samples = finiteSamples.data();
    finiteInput.layout = &layout;

    an::MaskedValidityInput zeroInput;
    zeroInput.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    zeroInput.sampleType = an::SampleType::Float32;
    zeroInput.samples = zeroSamples.data();
    zeroInput.layout = &layout;

    // Two rules that disagree combine to the mask rule, and the mask is the intersection of what
    // each of them says.
    const std::array<an::MaskedValidityInput, 2u> inputs{finiteInput, zeroInput};
    OwnedMask combined = ownedMask(kWidth, kHeight, 1u);
    const an::ValidityTransition combine{.policy = an::ValidityPolicy::Combine};
    const an::Result<an::ValidityDescriptor> answer =
        an::resolveImageValidity(execution, combine, inputs, viewOf(combined));
    ASSERT_TRUE(answer.hasValue()) << answer.status().message();
    EXPECT_EQ(answer->rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(constViewOf(combined).countValid(), static_cast<std::uint64_t>(kWidth) * kHeight - 2u);
    EXPECT_FALSE(constViewOf(combined).test(0u, 1u, 0u).value_or(true));
    EXPECT_FALSE(constViewOf(combined).test(0u, 2u, 0u).value_or(true));
    EXPECT_TRUE(constViewOf(combined).test(0u, 0u, 0u).value_or(false));

    // A destination mask supplied where the answer needs none is refused, and so is one missing
    // where the answer needs one.
    const std::array<an::MaskedValidityInput, 1u> one{finiteInput};
    OwnedMask unused = ownedMask(kWidth, kHeight, 1u);
    EXPECT_EQ(an::resolveImageValidity(execution, combine, one, viewOf(unused)).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::resolveImageValidity(execution, combine, inputs, std::nullopt).status().code(),
              an::ErrorCode::InvalidArgument);

    // One input that already carries a mask preserves it, which is a copy of that mask.
    OwnedMask sourceMask = ownedMask(kWidth, kHeight, 1u);
    for (std::uint32_t x = 0u; x < kWidth; ++x)
    {
        EXPECT_TRUE(viewOf(sourceMask).setValid(0u, x, 0u, x % 2u == 0u));
    }
    an::MaskedValidityInput maskedInput;
    maskedInput.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    maskedInput.mask = constViewOf(sourceMask);

    const std::array<an::MaskedValidityInput, 1u> masked{maskedInput};
    OwnedMask preserved = ownedMask(kWidth, kHeight, 1u, 0xFFu);
    const an::ValidityTransition preserve{.policy = an::ValidityPolicy::Preserve};
    const an::Result<an::ValidityDescriptor> kept =
        an::resolveImageValidity(execution, preserve, masked, viewOf(preserved));
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    EXPECT_EQ(kept->rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(preserved.bytes, sourceMask.bytes);

    // An input whose rule says a mask decides and that hands over no mask is refused rather than
    // read as fully valid.
    an::MaskedValidityInput maskless = maskedInput;
    maskless.mask = std::nullopt;
    const std::array<an::MaskedValidityInput, 1u> incomplete{maskless};
    OwnedMask never = ownedMask(kWidth, kHeight, 1u);
    EXPECT_EQ(an::resolveImageValidity(execution, preserve, incomplete, viewOf(never)).status().code(),
              an::ErrorCode::InvalidArgument);

    // A policy that refuses incomplete data still refuses it here.
    const an::ValidityTransition reject{.policy = an::ValidityPolicy::Reject};
    EXPECT_EQ(an::resolveImageValidity(execution, reject, one, std::nullopt).status().code(),
              an::ErrorCode::IncompatibleArguments);

    // More inputs than one operation's arguments is a stated limit rather than an overrun.
    const std::array<an::MaskedValidityInput, 9u> tooMany{finiteInput, finiteInput, finiteInput,
                                                          finiteInput, finiteInput, finiteInput,
                                                          finiteInput, finiteInput, finiteInput};
    EXPECT_EQ(an::resolveImageValidity(execution, combine, tooMany, std::nullopt).status().code(),
              an::ErrorCode::LimitExceeded);
}
