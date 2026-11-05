// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/ImageLayout.h.
//
// A layout is where a plausible descriptor becomes a wrong pointer: a row stride one element short
// of the row it describes reads the next row's first sample, and a total byte count that wrapped
// sizes an allocation smaller than the geometry written through it. Both are refusals here.
//
// The two arrangements the engine actually stores - one plane per channel, and one plane with the
// channels interleaved - are expressed by how many samples of a pixel live in one plane, rather
// than by an enumeration. What that arrangement means is a question for the descriptor that
// carries channel meaning; what it costs in bytes is this.

#include <AfterNightEngine/Core/ImageLayout.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace
{

constexpr std::uint32_t kMaxDimension = (std::numeric_limits<std::uint32_t>::max)();

an::EngineLimits unboundedLimits()
{
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageWidth = kMaxDimension;
    limits.maxImageHeight = kMaxDimension;
    limits.maxChannelCount = kMaxDimension;
    limits.maxImageBytes = (std::numeric_limits<std::uint64_t>::max)();
    return limits;
}

an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                         const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    return extent.value_or(an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()).value());
}

an::ImageLayoutRequest planarRequest(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount,
                                     std::uint32_t elementSizeBytes, std::uint64_t rowAlignmentBytes,
                                     const an::EngineLimits& limits)
{
    return an::ImageLayoutRequest{.extent = extentOf(width, height, channelCount, limits),
                                  .elementSizeBytes = elementSizeBytes,
                                  .samplesPerPixel = 1u,
                                  .planeCount = channelCount,
                                  .rowAlignmentBytes = rowAlignmentBytes};
}

TEST(ImageLayoutTest, DerivesAPlanarLayoutWithOnePlanePerChannel)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(planarRequest(640u, 480u, 3u, 4u, 1u, limits), limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->planeCount(), 3u);
    EXPECT_EQ(layout->samplesPerPixel(), 1u);
    EXPECT_EQ(layout->minimumRowBytes(), 640ull * 4ull);
    EXPECT_EQ(layout->rowStrideBytes(), 640ull * 4ull);
    EXPECT_EQ(layout->planeStrideBytes(), 640ull * 480ull * 4ull);
    EXPECT_EQ(layout->totalBytes(), 640ull * 480ull * 3ull * 4ull);
    EXPECT_TRUE(layout->isContiguous());
}

TEST(ImageLayoutTest, DerivesAnInterleavedLayoutWithOnePlane)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request{.extent = extentOf(640u, 480u, 3u, limits),
                                         .elementSizeBytes = 2u,
                                         .samplesPerPixel = 3u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->minimumRowBytes(), 640ull * 3ull * 2ull);
    EXPECT_EQ(layout->totalBytes(), 640ull * 480ull * 3ull * 2ull);
    EXPECT_TRUE(layout->isContiguous());
}

TEST(ImageLayoutTest, RefusesAPlaneCountThatDoesNotSpendEveryChannel)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request{.extent = extentOf(64u, 64u, 3u, limits),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 2u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    EXPECT_EQ(an::ImageLayout::check(request, limits), an::GeometryError::PlaneCountMismatch);
    EXPECT_FALSE(an::ImageLayout::create(request, limits).has_value());
}

TEST(ImageLayoutTest, RefusesAZeroInTheArrangement)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageExtent extent = extentOf(64u, 64u, 3u, limits);
    const an::ImageLayoutRequest noElement{
        .extent = extent, .elementSizeBytes = 0u, .samplesPerPixel = 1u, .planeCount = 3u, .rowAlignmentBytes = 1u};
    const an::ImageLayoutRequest noSamples{
        .extent = extent, .elementSizeBytes = 4u, .samplesPerPixel = 0u, .planeCount = 3u, .rowAlignmentBytes = 1u};
    const an::ImageLayoutRequest noPlanes{
        .extent = extent, .elementSizeBytes = 4u, .samplesPerPixel = 1u, .planeCount = 0u, .rowAlignmentBytes = 1u};
    EXPECT_EQ(an::ImageLayout::check(noElement, limits), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::ImageLayout::check(noSamples, limits), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::ImageLayout::check(noPlanes, limits), an::GeometryError::ZeroDimension);
}

TEST(ImageLayoutTest, RefusesAnAlignmentThatIsNotAPowerOfTwo)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    EXPECT_EQ(an::ImageLayout::check(planarRequest(64u, 64u, 1u, 4u, 24u, limits), limits),
              an::GeometryError::AlignmentNotPowerOfTwo);
    EXPECT_EQ(an::ImageLayout::check(planarRequest(64u, 64u, 1u, 4u, 0u, limits), limits),
              an::GeometryError::AlignmentNotPowerOfTwo);
}

TEST(ImageLayoutTest, RefusesAnElementSizeThatIsNotAPowerOfTwo)
{
    // Three bytes of packed colour are three samples of one byte. An element is one scalar sample,
    // and a size no scalar has would let a derived stride satisfy the alignment and not the
    // element size at once.
    const an::EngineLimits limits = an::defaultEngineLimits();
    EXPECT_EQ(an::ImageLayout::check(planarRequest(64u, 64u, 1u, 3u, 64u, limits), limits),
              an::GeometryError::ElementSizeNotPowerOfTwo);
    EXPECT_EQ(an::ImageLayout::check(planarRequest(64u, 64u, 1u, 6u, 1u, limits), limits),
              an::GeometryError::ElementSizeNotPowerOfTwo);
}

TEST(ImageLayoutTest, ADerivedLayoutIsNeverOneItWouldRefuse)
{
    // The property that makes deriving strides safe rather than merely convenient: whatever
    // element size and alignment a caller states, the strides this computes satisfy the rules it
    // checks stated strides against. A width chosen to be awkward on purpose - 101 samples pads
    // against every alignment above one.
    const an::EngineLimits limits = an::defaultEngineLimits();
    for (const std::uint32_t elementSize : {1u, 2u, 4u, 8u})
    {
        for (const std::uint64_t alignment : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u})
        {
            const an::ImageLayoutRequest request = planarRequest(101u, 7u, 2u, elementSize, alignment, limits);
            const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
            ASSERT_TRUE(layout.has_value()) << "element size " << elementSize << ", alignment " << alignment;
            EXPECT_EQ(an::ImageLayout::checkWithStrides(request, layout->rowStrideBytes(), layout->planeStrideBytes(),
                                                        limits),
                      an::GeometryError::None)
                << "element size " << elementSize << ", alignment " << alignment;
            EXPECT_GE(layout->rowStrideBytes(), layout->minimumRowBytes());
            EXPECT_TRUE(an::isAligned(layout->rowStrideBytes(), alignment));
            EXPECT_EQ(layout->rowStrideBytes() % elementSize, 0u);
        }
    }
}

TEST(ImageLayoutTest, ASizeThisPlatformCannotAddressIsRefusedBeforeAConfiguredMaximumIs)
{
    // Both rules apply: the total is over the configured maximum image size, and it is also larger
    // than a pointer difference can span. The fact is reported, not the policy, because raising
    // the maximum would not make this geometry usable.
    an::EngineLimits limits = unboundedLimits();
    limits.maxImageBytes = 1024ull * 1024ull;
    EXPECT_EQ(an::ImageLayout::check(planarRequest(kMaxDimension, 2147483649u, 1u, 1u, 1u, limits), limits),
              an::GeometryError::SizeNotRepresentable);
    EXPECT_EQ(an::ImageLayout::check(planarRequest(1024u, 1024u, 1u, 4u, 1u, limits), limits),
              an::GeometryError::ImageTooLarge);
}

TEST(ImageLayoutTest, ADerivedRowStrideIsTheSmallestOneTheAlignmentAllows)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    // 100 samples of 2 bytes is 200 bytes of pixels, which is not a multiple of the 64-byte line
    // the buffers are aligned to, so the row is padded to 256 and the padding is real memory that
    // the total has to account for.
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(planarRequest(100u, 10u, 1u, 2u, 64u, limits), limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->minimumRowBytes(), 200u);
    EXPECT_EQ(layout->rowStrideBytes(), 256u);
    EXPECT_FALSE(layout->isContiguous());
    // Nine full rows of stride, and a last row that only needs the samples in it.
    EXPECT_EQ(layout->totalBytes(), 9ull * 256ull + 200ull);
}

TEST(ImageLayoutTest, RefusesAStrideSmallerThanWhatItHasToHold)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request = planarRequest(100u, 10u, 2u, 2u, 1u, limits);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 200u, 2000u, limits), an::GeometryError::None);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 198u, 2000u, limits), an::GeometryError::StrideTooSmall);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 200u, 1998u, limits), an::GeometryError::StrideTooSmall);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 200u, 0u, limits), an::GeometryError::StrideTooSmall);
}

TEST(ImageLayoutTest, RefusesAStrideThatBreaksElementOrRequestedAlignment)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    // A stride that is not a whole number of elements puts every row after the first at an offset
    // no typed pointer can address, which is a wrong read rather than a slow one.
    const an::ImageLayoutRequest unaligned = planarRequest(100u, 10u, 1u, 4u, 1u, limits);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(unaligned, 402u, 4020u, limits), an::GeometryError::StrideMisaligned);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(unaligned, 404u, 4038u, limits), an::GeometryError::StrideMisaligned);

    const an::ImageLayoutRequest lineAligned = planarRequest(100u, 10u, 1u, 4u, 64u, limits);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(lineAligned, 448u, 4480u, limits), an::GeometryError::None);
    EXPECT_EQ(an::ImageLayout::checkWithStrides(lineAligned, 404u, 4480u, limits), an::GeometryError::StrideMisaligned);
}

TEST(ImageLayoutTest, StatedStridesAreKeptRatherThanNormalised)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(planarRequest(100u, 10u, 2u, 2u, 1u, limits), 512u, 8192u, limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->rowStrideBytes(), 512u);
    EXPECT_EQ(layout->planeStrideBytes(), 8192u);
    EXPECT_FALSE(layout->isContiguous());
    EXPECT_EQ(layout->totalBytes(), 8192ull + 9ull * 512ull + 200ull);
}

TEST(ImageLayoutTest, RefusesATotalOverTheConfiguredMaximumImageSize)
{
    an::EngineLimits limits = an::defaultEngineLimits();
    limits.maxImageBytes = 1024ull * 1024ull;
    EXPECT_EQ(an::ImageLayout::check(planarRequest(512u, 512u, 1u, 4u, 1u, limits), limits), an::GeometryError::None);
    EXPECT_EQ(an::ImageLayout::check(planarRequest(512u, 512u, 2u, 4u, 1u, limits), limits),
              an::GeometryError::ImageTooLarge);
}

TEST(ImageLayoutTest, RefusesATotalThatCannotBeCountedOrAddressed)
{
    const an::EngineLimits limits = unboundedLimits();
    // Large enough that the byte count overflows the arithmetic entirely.
    EXPECT_EQ(an::ImageLayout::check(planarRequest(kMaxDimension, kMaxDimension, 1u, 8u, 1u, limits), limits),
              an::GeometryError::ArithmeticOverflow);
    // Large enough to count, too large for a pointer difference on this platform, and refused for
    // that reason rather than by a configured maximum that has been raised out of the way.
    // One byte per sample, and one more row than a pointer difference can span: 4294967295 by
    // 2147483649 counts to just over 2^63 bytes, which is a number this arithmetic holds and this
    // platform cannot address.
    EXPECT_EQ(an::ImageLayout::check(planarRequest(kMaxDimension, 2147483649u, 1u, 1u, 1u, limits), limits),
              an::GeometryError::SizeNotRepresentable);
}

TEST(ImageLayoutTest, OffsetsWalkTheSamplesAndStopAtTheEdges)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request{.extent = extentOf(4u, 3u, 2u, limits),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 2u,
                                         .planeCount = 1u,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->sampleByteOffset(0u, 0u, 0u, 0u).value_or(1u), 0u);
    EXPECT_EQ(layout->sampleByteOffset(0u, 0u, 0u, 1u).value_or(0u), 4u);
    EXPECT_EQ(layout->sampleByteOffset(0u, 0u, 1u, 0u).value_or(0u), 8u);
    EXPECT_EQ(layout->sampleByteOffset(0u, 1u, 0u, 0u).value_or(0u), 32u);
    EXPECT_EQ(layout->rowByteOffset(0u, 2u).value_or(0u), 64u);

    // The last sample ends exactly where the buffer does, which is the property an allocation
    // sized from totalBytes() depends on.
    EXPECT_EQ(layout->sampleByteOffset(0u, 2u, 3u, 1u).value_or(0u) + layout->elementSizeBytes(), layout->totalBytes());

    EXPECT_FALSE(layout->sampleByteOffset(1u, 0u, 0u, 0u).has_value());
    EXPECT_FALSE(layout->sampleByteOffset(0u, 3u, 0u, 0u).has_value());
    EXPECT_FALSE(layout->sampleByteOffset(0u, 0u, 4u, 0u).has_value());
    EXPECT_FALSE(layout->sampleByteOffset(0u, 0u, 0u, 2u).has_value());
    EXPECT_FALSE(layout->rowByteOffset(0u, 3u).has_value());
}

TEST(ImageLayoutTest, OffsetsCrossPlanesByTheirStride)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(planarRequest(4u, 3u, 3u, 2u, 1u, limits), limits);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->planeStrideBytes(), 24u);
    EXPECT_EQ(layout->sampleByteOffset(1u, 0u, 0u, 0u).value_or(0u), 24u);
    EXPECT_EQ(layout->sampleByteOffset(2u, 2u, 3u, 0u).value_or(0u) + layout->elementSizeBytes(), layout->totalBytes());
    EXPECT_FALSE(layout->sampleByteOffset(3u, 0u, 0u, 0u).has_value());
}

TEST(ImageLayoutTest, IsBuiltAtCompileTime)
{
    constexpr std::optional<an::ImageExtent> kExtent =
        an::ImageExtent::create(1024u, 768u, 1u, an::defaultEngineLimits());
    static_assert(kExtent.has_value());
    constexpr an::ImageLayoutRequest kRequest{.extent = *kExtent,
                                              .elementSizeBytes = 4u,
                                              .samplesPerPixel = 1u,
                                              .planeCount = 1u,
                                              .rowAlignmentBytes = an::kDefaultBufferAlignmentBytes};
    constexpr std::optional<an::ImageLayout> kLayout = an::ImageLayout::create(kRequest, an::defaultEngineLimits());
    static_assert(kLayout.has_value());
    static_assert(kLayout->totalBytes() == 1024ull * 768ull * 4ull);
    EXPECT_TRUE(kLayout->isContiguous());
}

} // namespace

TEST(ImageLayoutTest, IntrinsicLayoutErrorsPrecedeConfiguredLimits)
{
    auto limits = an::defaultEngineLimits();
    auto request = planarRequest(8u, 8u, 1u, 4u, 1u, limits);
    limits.maxImageWidth = 4u;
    request.rowAlignmentBytes = 3u;
    EXPECT_EQ(an::ImageLayout::check(request, limits), an::GeometryError::AlignmentNotPowerOfTwo);
    request.rowAlignmentBytes = 1u;
    EXPECT_EQ(an::ImageLayout::checkWithStrides(request, 1u, 64u, limits), an::GeometryError::StrideTooSmall);
    EXPECT_EQ(an::ImageLayout::check(request, limits), an::GeometryError::DimensionTooLarge);
}
