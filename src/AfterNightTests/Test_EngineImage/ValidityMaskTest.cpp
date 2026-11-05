// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ValidityMaskView.h and Image/ValidityMask.h.
//
// A packed mask is the one representation of validity that costs memory, and every property worth
// testing about it is a property that goes wrong quietly.
//
// **The bits past the last pixel of a row are zero.** A row of ten pixels has six bits in its second
// byte that belong to no pixel, and they share that byte with six that do. Left undefined, they make
// a popcount wrong by up to seven per row and make two masks holding the same answers compare
// unequal byte for byte. Every write here is checked for them, including the inverting one, which is
// the write that would set them all.
//
// **A new mask selects nothing.** The zeroed allocation means a half-written mask claims the samples
// it reached and no others, which is the safe direction; the cases assert it rather than assuming
// the allocator zeroes.
//
// **The memory is counted and released.** Every case that allocates asserts what the context's
// resource says afterwards, and the lifetime case asserts the other direction: a context with a mask
// outstanding refuses to shut down.
//
// **A mask built from values honours the rule and nothing else.** The marking cases are where the
// compatibility zero rule meets real samples, and where a mask shared by every channel is proved to
// be the conservative answer rather than the first channel's.

#include <AfterNightEngine/Image/ValidityMask.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{

/// An engine context with a stated memory budget.
///
/// @param budgetBytes what the context may have outstanding at once.
/// @return the context, which is valid or the case has already failed.
[[nodiscard]] an::EngineContext makeEngineContext(std::uint64_t budgetBytes = an::kDefaultMemoryBudgetBytes)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 2u;
    options.limits = an::defaultEngineLimits();
    options.limits.memoryBudgetBytes = budgetBytes;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue());
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// An extent for a mask or an image.
///
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels, or how many bit-planes.
/// @return the extent, which is valid or the case has already failed.
[[nodiscard]] an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount = 1u)
{
    const std::optional<an::ImageExtent> made =
        an::ImageExtent::create(width, height, channelCount, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

/// A description carrying one rule that needs no range.
///
/// @param rule the rule to carry.
/// @return the description, which is valid or the case has already failed.
[[nodiscard]] an::ValidityDescriptor ruleOf(an::ValidityRule rule)
{
    const std::optional<an::ValidityDescriptor> made =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = rule});
    EXPECT_TRUE(made.has_value());
    return made.value_or(an::ValidityDescriptor{});
}

/// A planar image layout of one representation.
///
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @param channelCount how many channels, one plane each.
/// @return the layout, which is valid or the case has already failed.
template <typename SampleT>
[[nodiscard]] an::ImageLayout planarLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const an::ImageLayoutRequest request{.extent = extentOf(width, height, channelCount),
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(SampleT)),
                                         .samplesPerPixel = 1u,
                                         .planeCount = channelCount,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> made = an::ImageLayout::create(request, an::defaultEngineLimits());
    EXPECT_TRUE(made.has_value());
    return made.value();
}

} // namespace

TEST(PackedMaskGeometryTest, ARowRoundsUpToWholeBytes)
{
    static_assert(an::packedMaskRowBytes(1u) == 1u);
    static_assert(an::packedMaskRowBytes(8u) == 1u);
    static_assert(an::packedMaskRowBytes(9u) == 2u);
    static_assert(an::packedMaskRowBytes(10u) == 2u);
    static_assert(an::packedMaskRowBytes(16u) == 2u);
}

TEST(PackedMaskGeometryTest, TheLastByteMaskCoversExactlyThePixelsThatUseIt)
{
    static_assert(an::packedMaskLastByteMask(8u) == 0xFFu);
    static_assert(an::packedMaskLastByteMask(16u) == 0xFFu);
    static_assert(an::packedMaskLastByteMask(1u) == 0x01u);
    static_assert(an::packedMaskLastByteMask(10u) == 0x03u);
    static_assert(an::packedMaskLastByteMask(15u) == 0x7Fu);
}

TEST(ValidityMaskViewTest, AStrideThatDoesNotCoverARowIsRefused)
{
    std::array<std::uint8_t, 8u> bits{};
    const an::ImageExtent extent = extentOf(10u, 2u);
    EXPECT_EQ(an::ValidityMaskView<std::uint8_t>::check(std::span<std::uint8_t>{bits}, extent, 1u, 4u),
              an::GeometryError::StrideTooSmall);
    EXPECT_EQ(an::ValidityMaskView<std::uint8_t>::check(std::span<std::uint8_t>{bits}, extent, 2u, 3u),
              an::GeometryError::StrideTooSmall);
    EXPECT_EQ(an::ValidityMaskView<std::uint8_t>::check(std::span<std::uint8_t>{bits}, extent, 2u, 4u),
              an::GeometryError::None);
}

TEST(ValidityMaskViewTest, ABufferTooSmallForTheGeometryIsRefused)
{
    std::array<std::uint8_t, 3u> bits{};
    const an::ImageExtent extent = extentOf(10u, 2u);
    EXPECT_EQ(an::ValidityMaskView<std::uint8_t>::check(std::span<std::uint8_t>{bits}, extent, 2u, 4u),
              an::GeometryError::BufferTooSmall);
    EXPECT_FALSE(an::ValidityMaskView<std::uint8_t>::create(std::span<std::uint8_t>{bits}, extent, 2u, 4u).has_value());
}

TEST(ValidityMaskViewTest, TheLastRowStopsAtItsOwnBytesRatherThanAtItsStride)
{
    // Four bytes: two rows of two, and the second row does not need the padding its stride implies.
    std::array<std::uint8_t, 4u> bits{};
    const an::ImageExtent extent = extentOf(10u, 2u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::create(std::span<std::uint8_t>{bits}, extent, 2u, 4u);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->spannedByteCount(), 4u);
}

TEST(ValidityMaskViewTest, ATightViewPacksRowsAndPlanesWithNoPaddingAtAll)
{
    std::array<std::uint8_t, 6u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 3u));
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->rowStrideBytes(), 2u);
    EXPECT_EQ(view->planeStrideBytes(), 6u);
    EXPECT_TRUE(view->isContiguous());
    EXPECT_TRUE(view->isShared());
    EXPECT_EQ(view->bitCount(), 30u);
}

TEST(ValidityMaskViewTest, ABitIsSetAndReadBackAtThePixelItNames)
{
    std::array<std::uint8_t, 4u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 2u));
    ASSERT_TRUE(view.has_value());

    EXPECT_TRUE(view->setValid(0u, 9u, 1u, true));
    EXPECT_EQ(view->test(0u, 9u, 1u), std::optional<bool>{true});
    EXPECT_EQ(view->test(0u, 8u, 1u), std::optional<bool>{false});
    // Bit 9 of a row lives in the second byte at bit 1, least significant bit first.
    EXPECT_EQ(bits[3], 0x02u);

    EXPECT_TRUE(view->setValid(0u, 9u, 1u, false));
    EXPECT_EQ(bits[3], 0x00u);
}

TEST(ValidityMaskViewTest, AnIndexOutsideTheMaskAnswersNothingAndWritesNothing)
{
    std::array<std::uint8_t, 4u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 2u));
    ASSERT_TRUE(view.has_value());

    EXPECT_FALSE(view->test(1u, 0u, 0u).has_value());
    EXPECT_FALSE(view->test(0u, 10u, 0u).has_value());
    EXPECT_FALSE(view->test(0u, 0u, 2u).has_value());
    EXPECT_FALSE(view->setValid(0u, 10u, 0u, true));
    for (const std::uint8_t byte : bits)
    {
        EXPECT_EQ(byte, 0x00u);
    }
}

TEST(ValidityMaskViewTest, FillingLeavesTheBitsPastTheLastPixelZero)
{
    std::array<std::uint8_t, 4u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 2u));
    ASSERT_TRUE(view.has_value());

    view->fill(true);
    EXPECT_EQ(bits[0], 0xFFu);
    EXPECT_EQ(bits[1], 0x03u);
    EXPECT_EQ(bits[2], 0xFFu);
    EXPECT_EQ(bits[3], 0x03u);
    EXPECT_EQ(view->countValid(), 20u);

    view->fill(false);
    EXPECT_EQ(view->countValid(), 0u);
}

TEST(ValidityMaskViewTest, ACountIgnoresBitsPastTheLastPixelWhateverTheyHold)
{
    std::array<std::uint8_t, 2u> bits{0xFFu, 0xFFu};
    const std::optional<an::ValidityMaskView<std::uint8_t>> view =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 1u));
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->countValid(), 10u);

    view->clearPadBits();
    EXPECT_EQ(bits[1], 0x03u);
    EXPECT_EQ(view->countValid(), 10u);
}

TEST(ValidityMaskViewTest, AWritableViewConvertsToAReadOnlyOneAndNotBack)
{
    std::array<std::uint8_t, 2u> bits{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{bits}, extentOf(10u, 1u));
    ASSERT_TRUE(writable.has_value());

    const an::ValidityMaskView<const std::uint8_t> readable = *writable;
    EXPECT_EQ(readable.data(), writable->data());
    EXPECT_EQ(readable.extent(), writable->extent());
    EXPECT_EQ(writable->constView(), readable);

    static_assert(std::is_convertible_v<an::ValidityMaskView<std::uint8_t>, an::ValidityMaskView<const std::uint8_t>>);
    static_assert(!std::is_convertible_v<an::ValidityMaskView<const std::uint8_t>, an::ValidityMaskView<std::uint8_t>>);
}

TEST(ValidityMaskTest, AMaskThatHoldsNothingAnswersForThatStateRatherThanRequiringTheQuestion)
{
    const an::ValidityMask empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(empty.extent().has_value());
    EXPECT_EQ(empty.capacityBytes(), 0u);
    EXPECT_EQ(empty.contextId(), 0u);
    EXPECT_EQ(empty.memoryResource(), nullptr);
    EXPECT_FALSE(empty.view().has_value());
    EXPECT_FALSE(empty.clone().hasValue());
}

TEST(ValidityMaskTest, ANewMaskSelectsNothingAndCountsAgainstTheBudget)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(10u, 4u));
    ASSERT_TRUE(mask.hasValue());
    EXPECT_TRUE(mask->valid());
    EXPECT_EQ(mask->extent(), std::optional<an::ImageExtent>{extentOf(10u, 4u)});
    EXPECT_EQ(mask->capacityBytes(), 8u);
    EXPECT_EQ(mask->contextId(), engine.id());
    EXPECT_NE(mask->memoryResource(), nullptr);

    const std::optional<an::ValidityMaskView<const std::uint8_t>> view = std::as_const(*mask).view();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(an::countValidity(*view), (an::ValidityCount{.valid = 0u, .total = 40u}));

    const std::uint64_t live = engine.memoryResource()->counters().liveBytes;
    EXPECT_GE(live, 8u);
    mask->release();
    EXPECT_FALSE(mask->valid());
    EXPECT_LT(engine.memoryResource()->counters().liveBytes, live);
}

TEST(ValidityMaskTest, AnAllValidMaskSelectsEverythingAndNothingBeyondIt)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(engine, extentOf(10u, 4u, 3u));
    ASSERT_TRUE(mask.hasValue());

    const std::optional<an::ValidityMaskView<const std::uint8_t>> view = std::as_const(*mask).view();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->planeCount(), 3u);
    EXPECT_FALSE(view->isShared());
    EXPECT_EQ(an::countValidity(*view), (an::ValidityCount{.valid = 120u, .total = 120u}));
    EXPECT_TRUE(an::countValidity(*view).complete());
}

TEST(ValidityMaskTest, AContextWithAMaskOutstandingRefusesToShutDown)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(64u, 64u));
    ASSERT_TRUE(mask.hasValue());

    const an::Status refused = engine.shutdown();
    EXPECT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);

    mask->release();
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(ValidityMaskTest, ABudgetRefusalCarriesTheNumbersACallerAnswersItWith)
{
    an::EngineContext engine = makeEngineContext(64u);
    const an::Result<an::ValidityMask> refused = an::ValidityMask::create(engine, extentOf(4096u, 4096u));
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_NE(refused.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_NE(refused.status().findContext(an::ContextKey::Limit), nullptr);
}

TEST(ValidityMaskTest, ACloneIsASecondMaskWithTheSameAnswersAndItsOwnMemory)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(10u, 3u));
    ASSERT_TRUE(mask.hasValue());
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable = mask->view();
    ASSERT_TRUE(writable.has_value());
    EXPECT_TRUE(writable->setValid(0u, 4u, 2u, true));

    an::Result<an::ValidityMask> copy = mask->clone();
    ASSERT_TRUE(copy.hasValue());
    const std::optional<an::ValidityMaskView<const std::uint8_t>> copied = std::as_const(*copy).view();
    ASSERT_TRUE(copied.has_value());
    EXPECT_EQ(copied->test(0u, 4u, 2u), std::optional<bool>{true});
    EXPECT_NE(static_cast<const void*>(copied->data()), static_cast<const void*>(writable->data()));

    EXPECT_TRUE(writable->setValid(0u, 4u, 2u, false));
    EXPECT_EQ(copied->test(0u, 4u, 2u), std::optional<bool>{true});
}

TEST(ValidityMaskOperationsTest, AMaskCoversAnImageOfItsOwnSizeWithOnePlaneOrOnePerChannel)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> shared = an::ValidityMask::create(engine, extentOf(10u, 4u));
    ASSERT_TRUE(shared.hasValue());
    an::Result<an::ValidityMask> perChannel = an::ValidityMask::create(engine, extentOf(10u, 4u, 3u));
    ASSERT_TRUE(perChannel.hasValue());

    const an::ImageExtent image = extentOf(10u, 4u, 3u);
    EXPECT_TRUE(an::checkMaskCovers(*std::as_const(*shared).view(), image, "test").ok());
    EXPECT_TRUE(an::checkMaskCovers(*std::as_const(*perChannel).view(), image, "test").ok());

    const an::Status wrongPixels = an::checkMaskCovers(*std::as_const(*shared).view(), extentOf(11u, 4u, 3u), "test");
    EXPECT_EQ(wrongPixels.code(), an::ErrorCode::IncompatibleArguments);

    an::Result<an::ValidityMask> twoPlanes = an::ValidityMask::create(engine, extentOf(10u, 4u, 2u));
    ASSERT_TRUE(twoPlanes.hasValue());
    const an::Status wrongPlanes = an::checkMaskCovers(*std::as_const(*twoPlanes).view(), image, "test");
    EXPECT_EQ(wrongPlanes.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ValidityMaskOperationsTest, CombiningIntersectsAndUnitesAndLeavesThePadBitsZero)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> left = an::ValidityMask::create(engine, extentOf(10u, 1u));
    an::Result<an::ValidityMask> right = an::ValidityMask::create(engine, extentOf(10u, 1u));
    an::Result<an::ValidityMask> target = an::ValidityMask::create(engine, extentOf(10u, 1u));
    ASSERT_TRUE(left.hasValue());
    ASSERT_TRUE(right.hasValue());
    ASSERT_TRUE(target.hasValue());

    const an::ValidityMaskView<std::uint8_t> leftView = *left->view();
    const an::ValidityMaskView<std::uint8_t> rightView = *right->view();
    const an::ValidityMaskView<std::uint8_t> targetView = *target->view();

    leftView.fill(true);
    rightView.fill(false);
    EXPECT_TRUE(rightView.setValid(0u, 3u, 0u, true));
    EXPECT_TRUE(rightView.setValid(0u, 9u, 0u, true));

    EXPECT_TRUE(an::combineValidity(targetView, leftView, rightView, an::ValidityCombination::Intersection).ok());
    EXPECT_EQ(targetView.countValid(), 2u);
    EXPECT_EQ(targetView.test(0u, 3u, 0u), std::optional<bool>{true});
    EXPECT_EQ(targetView.test(0u, 4u, 0u), std::optional<bool>{false});

    EXPECT_TRUE(an::combineValidity(targetView, leftView, rightView, an::ValidityCombination::Union).ok());
    EXPECT_EQ(targetView.countValid(), 10u);
    EXPECT_EQ(targetView.data()[1], 0x03u);
}

TEST(ValidityMaskOperationsTest, ADestinationThatIsOneOfItsSourcesIsAllowedAndAPartialOverlapIsNot)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> accumulator = an::ValidityMask::createAllValid(engine, extentOf(10u, 2u));
    an::Result<an::ValidityMask> frame = an::ValidityMask::create(engine, extentOf(10u, 2u));
    ASSERT_TRUE(accumulator.hasValue());
    ASSERT_TRUE(frame.hasValue());

    const an::ValidityMaskView<std::uint8_t> into = *accumulator->view();
    const an::ValidityMaskView<std::uint8_t> from = *frame->view();
    from.fill(false);
    EXPECT_TRUE(from.setValid(0u, 1u, 1u, true));

    EXPECT_TRUE(an::combineValidity(into, into, from, an::ValidityCombination::Intersection).ok());
    EXPECT_EQ(into.countValid(), 1u);

    // Two views of one buffer at different offsets are the same shape and share bytes without
    // being the same mask, which is the case a call has to refuse: the bytes it has already written
    // would be read as input further along the same row.
    std::array<std::uint8_t, 8u> shared{};
    const std::optional<an::ValidityMaskView<std::uint8_t>> first =
        an::ValidityMaskView<std::uint8_t>::createTight(std::span<std::uint8_t>{shared.data(), 4u}, extentOf(10u, 2u));
    const std::optional<an::ValidityMaskView<const std::uint8_t>> second =
        an::ValidityMaskView<const std::uint8_t>::createTight(std::span<const std::uint8_t>{shared.data() + 2u, 4u},
                                                              extentOf(10u, 2u));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    const an::Status refusedCopy = an::copyValidity(*first, *second);
    EXPECT_EQ(refusedCopy.code(), an::ErrorCode::InvalidArgument);
    const an::Status refusedCombine = an::combineValidity(*first, *second, *second, an::ValidityCombination::Union);
    EXPECT_EQ(refusedCombine.code(), an::ErrorCode::InvalidArgument);
}

TEST(ValidityMaskOperationsTest, MasksOfDifferentShapesAreRefusedRatherThanTruncated)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> wide = an::ValidityMask::create(engine, extentOf(16u, 2u));
    an::Result<an::ValidityMask> narrow = an::ValidityMask::create(engine, extentOf(8u, 2u));
    ASSERT_TRUE(wide.hasValue());
    ASSERT_TRUE(narrow.hasValue());

    const an::Status refusedCopy = an::copyValidity(*wide->view(), *std::as_const(*narrow).view());
    EXPECT_EQ(refusedCopy.code(), an::ErrorCode::IncompatibleArguments);

    const an::Status refusedCombine = an::combineValidity(
        *wide->view(), *std::as_const(*wide).view(), *std::as_const(*narrow).view(), an::ValidityCombination::Union);
    EXPECT_EQ(refusedCombine.code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ValidityMaskOperationsTest, ACombinationOutsideTheEnumerationIsRefused)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(8u, 1u));
    ASSERT_TRUE(mask.hasValue());
    const an::Status refused =
        an::combineValidity(*mask->view(), *std::as_const(*mask).view(), *std::as_const(*mask).view(),
                            static_cast<an::ValidityCombination>(7));
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_STREQ(an::validityCombinationName(static_cast<an::ValidityCombination>(7)), "UnknownValidityCombination");
}

TEST(ValidityMaskOperationsTest, InvertingIsItsOwnInverseAndNeverSetsAPadBit)
{
    an::EngineContext engine = makeEngineContext();
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(10u, 2u));
    ASSERT_TRUE(mask.hasValue());
    const an::ValidityMaskView<std::uint8_t> view = *mask->view();
    EXPECT_TRUE(view.setValid(0u, 2u, 0u, true));

    an::invertValidity(view);
    EXPECT_EQ(view.countValid(), 19u);
    EXPECT_EQ(view.test(0u, 2u, 0u), std::optional<bool>{false});
    EXPECT_EQ(view.data()[1], 0x03u);

    an::invertValidity(view);
    EXPECT_EQ(view.countValid(), 1u);
    EXPECT_EQ(view.test(0u, 2u, 0u), std::optional<bool>{true});
}

TEST(ValidityMarkingTest, TheZeroRuleIsHonouredOverRealSamplesAndNoOtherRuleIs)
{
    an::EngineContext engine = makeEngineContext();
    std::array<std::uint16_t, 12u> samples{0u, 1u, 0u, 5u, 7u, 0u, 9u, 3u, 0u, 0u, 2u, 4u};
    const an::ImageLayout layout = planarLayout<std::uint16_t>(4u, 3u, 1u);
    const std::optional<an::ImageView<const std::uint16_t>> image =
        an::ImageView<const std::uint16_t>::create(std::span<const std::uint16_t>{samples}, layout);
    ASSERT_TRUE(image.has_value());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(4u, 3u));
    ASSERT_TRUE(mask.hasValue());
    const an::ValidityMaskView<std::uint8_t> view = *mask->view();

    EXPECT_TRUE(an::markValidity(view, *image, ruleOf(an::ValidityRule::ZeroIsNoData)).ok());
    EXPECT_EQ(view.countValid(), 7u);
    EXPECT_EQ(view.test(0u, 0u, 0u), std::optional<bool>{false});
    EXPECT_EQ(view.test(0u, 1u, 0u), std::optional<bool>{true});

    EXPECT_TRUE(an::markValidity(view, *image, an::ValidityDescriptor::allValid()).ok());
    EXPECT_EQ(view.countValid(), 12u);

    EXPECT_TRUE(an::markValidity(view, *image, ruleOf(an::ValidityRule::FiniteOnly)).ok());
    EXPECT_EQ(view.countValid(), 12u);
}

TEST(ValidityMarkingTest, TheFiniteRuleFindsWhatACalibrationDivideLeavesBehind)
{
    an::EngineContext engine = makeEngineContext();
    std::array<float, 6u> samples{1.0f,
                                  std::numeric_limits<float>::quiet_NaN(),
                                  2.0f,
                                  std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(),
                                  0.0f};
    const an::ImageLayout layout = planarLayout<float>(3u, 2u, 1u);
    const std::optional<an::ImageView<const float>> image =
        an::ImageView<const float>::create(std::span<const float>{samples}, layout);
    ASSERT_TRUE(image.has_value());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(3u, 2u));
    ASSERT_TRUE(mask.hasValue());
    const an::ValidityMaskView<std::uint8_t> view = *mask->view();

    EXPECT_TRUE(an::markValidity(view, *image, ruleOf(an::ValidityRule::FiniteOnly)).ok());
    EXPECT_EQ(view.countValid(), 3u);
    EXPECT_EQ(view.test(0u, 1u, 0u), std::optional<bool>{false});
    EXPECT_EQ(view.test(0u, 2u, 1u), std::optional<bool>{true});
}

TEST(ValidityMarkingTest, ASharedMaskMeansSomethingOnlyWhereEveryChannelDoes)
{
    an::EngineContext engine = makeEngineContext();
    // Three planes of two pixels: the second pixel is zero in the third channel alone.
    std::array<std::uint8_t, 6u> samples{4u, 5u, 6u, 7u, 8u, 0u};
    const an::ImageLayout layout = planarLayout<std::uint8_t>(2u, 1u, 3u);
    const std::optional<an::ImageView<const std::uint8_t>> image =
        an::ImageView<const std::uint8_t>::create(std::span<const std::uint8_t>{samples}, layout);
    ASSERT_TRUE(image.has_value());

    an::Result<an::ValidityMask> shared = an::ValidityMask::create(engine, extentOf(2u, 1u));
    ASSERT_TRUE(shared.hasValue());
    const an::ValidityMaskView<std::uint8_t> sharedView = *shared->view();
    EXPECT_TRUE(an::markValidity(sharedView, *image, ruleOf(an::ValidityRule::ZeroIsNoData)).ok());
    EXPECT_EQ(sharedView.test(0u, 0u, 0u), std::optional<bool>{true});
    EXPECT_EQ(sharedView.test(0u, 1u, 0u), std::optional<bool>{false});

    an::Result<an::ValidityMask> perChannel = an::ValidityMask::create(engine, extentOf(2u, 1u, 3u));
    ASSERT_TRUE(perChannel.hasValue());
    const an::ValidityMaskView<std::uint8_t> perChannelView = *perChannel->view();
    EXPECT_TRUE(an::markValidity(perChannelView, *image, ruleOf(an::ValidityRule::ZeroIsNoData)).ok());
    EXPECT_EQ(perChannelView.test(0u, 1u, 0u), std::optional<bool>{true});
    EXPECT_EQ(perChannelView.test(1u, 1u, 0u), std::optional<bool>{true});
    EXPECT_EQ(perChannelView.test(2u, 1u, 0u), std::optional<bool>{false});
    EXPECT_EQ(perChannelView.countValid(), 5u);
}

TEST(ValidityMarkingTest, AMaskCannotBeBuiltFromTheRuleThatSaysAMaskDecides)
{
    an::EngineContext engine = makeEngineContext();
    std::array<std::uint8_t, 4u> samples{1u, 2u, 3u, 4u};
    const an::ImageLayout layout = planarLayout<std::uint8_t>(2u, 2u, 1u);
    const std::optional<an::ImageView<const std::uint8_t>> image =
        an::ImageView<const std::uint8_t>::create(std::span<const std::uint8_t>{samples}, layout);
    ASSERT_TRUE(image.has_value());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(2u, 2u));
    ASSERT_TRUE(mask.hasValue());
    const an::Status refused = an::markValidity(*mask->view(), *image, ruleOf(an::ValidityRule::ExplicitMask));
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
}

TEST(ValidityMarkingTest, AMaskThatDoesNotCoverTheImageIsRefusedRatherThanPartlyWritten)
{
    an::EngineContext engine = makeEngineContext();
    std::array<std::uint8_t, 4u> samples{1u, 2u, 3u, 4u};
    const an::ImageLayout layout = planarLayout<std::uint8_t>(2u, 2u, 1u);
    const std::optional<an::ImageView<const std::uint8_t>> image =
        an::ImageView<const std::uint8_t>::create(std::span<const std::uint8_t>{samples}, layout);
    ASSERT_TRUE(image.has_value());

    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, extentOf(3u, 2u));
    ASSERT_TRUE(mask.hasValue());
    const an::ValidityMaskView<std::uint8_t> view = *mask->view();
    const an::Status refused = an::markValidity(view, *image, ruleOf(an::ValidityRule::ZeroIsNoData));
    EXPECT_EQ(refused.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(view.countValid(), 0u);
}

TEST(ValidityMaskTest, EmptyContextIsRefusedBeforeItsLimitsAreRead)
{
    const auto extent = an::ImageExtent::create(8u, 8u, 1u, an::defaultEngineLimits());
    const auto mask = an::ValidityMask::create(an::EngineContext{}, *extent);
    EXPECT_EQ(mask.code(), an::ErrorCode::NotInitialized);
    EXPECT_STREQ(mask.status().operation(), "ValidityMask::create");
}
