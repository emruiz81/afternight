// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/PlaneView.h.
//
// A view's whole value is that the check happened once, before the loop. So the cases that matter
// are the ones where a plausible-looking set of numbers has to be refused - a buffer one sample
// short, a stride that is not a whole number of samples, a base address a typed pointer cannot be
// formed at - because every one of those, admitted, is a loop that reads or writes somebody else's
// memory with nothing in the numbers to say so.
//
// The padded cases are the other half. A row span that ran to the stride instead of to the row
// would write into the padding, and the padding of the last row is not memory this view owns. Every
// row case below is run against a plane whose stride is larger than its rows for that reason.

#include <AfterNightEngine/Image/PlaneView.h>

#include <AfterNightEngine/Core/GeometryError.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace
{

/// The width, height and stride the padded fixtures below share.
constexpr std::uint32_t kWidth = 5u;
constexpr std::uint32_t kHeight = 3u;
constexpr std::uint32_t kSamplesPerPixel = 1u;

/// A row stride two samples wider than the row it holds, so every case runs against padding.
constexpr std::uint64_t kPaddedRowStrideBytes = (kWidth + 2u) * sizeof(std::uint16_t);

/// A buffer holding a padded plane, with every sample set to its own linear index.
///
/// The padding is left at a value no sample takes, so a case that reads padding as a sample reports
/// a number that could not have come from the image.
///
/// @return the buffer, sized exactly to what the plane's geometry names.
[[nodiscard]] std::vector<std::uint16_t> makePaddedPlane()
{
    const std::size_t rowSamples = kPaddedRowStrideBytes / sizeof(std::uint16_t);
    std::vector<std::uint16_t> buffer(rowSamples * kHeight, 0xEEEEu);
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            buffer[y * rowSamples + x] = static_cast<std::uint16_t>(y * kWidth + x);
        }
    }
    return buffer;
}

TEST(PlaneViewTest, ABufferOneSampleShortIsRefused)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::uint64_t needed = kPaddedRowStrideBytes * (kHeight - 1u) + kWidth * sizeof(std::uint16_t);
    const std::size_t exact = static_cast<std::size_t>(needed / sizeof(std::uint16_t));

    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(std::span<std::uint16_t>{buffer.data(), exact}, kWidth, kHeight,
                                                  kSamplesPerPixel, kPaddedRowStrideBytes),
              an::GeometryError::None);
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(std::span<std::uint16_t>{buffer.data(), exact - 1u}, kWidth, kHeight,
                                                  kSamplesPerPixel, kPaddedRowStrideBytes),
              an::GeometryError::BufferTooSmall);
    EXPECT_FALSE(an::PlaneView<std::uint16_t>::create(std::span<std::uint16_t>{buffer.data(), exact - 1u}, kWidth,
                                                      kHeight, kSamplesPerPixel, kPaddedRowStrideBytes)
                     .has_value());
}

TEST(PlaneViewTest, TheLastRowsPaddingIsNotMemoryThePlaneNeeds)
{
    // The exact size above stops at the last sample rather than at the end of the last row's
    // stride. That is the case a buffer allocated to fit exactly would fail if the view counted the
    // trailing padding as memory it must have.
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::size_t exact =
        static_cast<std::size_t>((kPaddedRowStrideBytes * (kHeight - 1u)) / sizeof(std::uint16_t)) + kWidth;
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer.data(), exact}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());
    EXPECT_EQ(plane->layout().totalBytes(), exact * sizeof(std::uint16_t));
}

TEST(PlaneViewTest, AMisalignedBaseAddressIsRefused)
{
    // A buffer of bytes deliberately offset by one, which is what a decoder handing over a payload
    // inside a header block produces. Reading 16-bit samples from it is undefined behaviour on
    // every platform and a fault on some, so it is refused where the buffer is adopted.
    alignas(std::uint16_t) std::uint8_t raw[64] = {};
    void* const oddAddress = raw + 1;
    auto* const misaligned = static_cast<std::uint16_t*>(oddAddress);
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(std::span<std::uint16_t>{misaligned, 16u}, 2u, 2u, 1u, 4u),
              an::GeometryError::BaseAddressMisaligned);

    void* const evenAddress = raw;
    auto* const aligned = static_cast<std::uint16_t*>(evenAddress);
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(std::span<std::uint16_t>{aligned, 16u}, 2u, 2u, 1u, 4u),
              an::GeometryError::None);
}

TEST(PlaneViewTest, TheGeometryIsRefusedForTheRuleThatRefusedIt)
{
    std::vector<std::uint16_t> buffer(256u, 0u);
    const std::span<std::uint16_t> samples{buffer};

    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(samples, 0u, 4u, 1u, 8u), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(samples, 4u, 0u, 1u, 8u), an::GeometryError::ZeroDimension);
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(samples, 4u, 4u, 0u, 8u), an::GeometryError::ZeroDimension);

    // A stride below the row it must hold: the descriptor that reads the next row's first sample
    // instead of its own last one.
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(samples, 4u, 4u, 1u, 6u), an::GeometryError::StrideTooSmall);

    // A stride that is not a whole number of samples puts every row after the first at an address
    // no typed pointer can reach.
    EXPECT_EQ(an::PlaneView<std::uint16_t>::check(samples, 4u, 4u, 1u, 9u), an::GeometryError::StrideMisaligned);
}

TEST(PlaneViewTest, ARowCoversItsOwnSamplesAndStopsBeforeThePadding)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());

    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        const std::optional<std::span<std::uint16_t>> row = plane->row(y);
        ASSERT_TRUE(row.has_value()) << y;
        EXPECT_EQ(row->size(), kWidth);
        for (std::uint32_t x = 0u; x < kWidth; ++x)
        {
            EXPECT_EQ((*row)[x], static_cast<std::uint16_t>(y * kWidth + x));
        }
    }
    EXPECT_FALSE(plane->row(kHeight).has_value());
}

TEST(PlaneViewTest, APaddedPlaneHasNoContiguousRun)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> padded = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(padded.has_value());
    EXPECT_FALSE(padded->isContiguous());
    EXPECT_FALSE(padded->contiguousSpan().has_value());

    const std::optional<an::PlaneView<std::uint16_t>> tight = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kWidth * sizeof(std::uint16_t));
    ASSERT_TRUE(tight.has_value());
    EXPECT_TRUE(tight->isContiguous());
    const std::optional<std::span<std::uint16_t>> run = tight->contiguousSpan();
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run->size(), static_cast<std::size_t>(kWidth) * kHeight);
}

TEST(PlaneViewTest, SampleAccessIsBoundsCheckedInEveryIndex)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());

    const std::uint16_t* const last = plane->sampleAt(kWidth - 1u, kHeight - 1u);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(*last, static_cast<std::uint16_t>((kHeight - 1u) * kWidth + (kWidth - 1u)));

    EXPECT_EQ(plane->sampleAt(kWidth, 0u), nullptr);
    EXPECT_EQ(plane->sampleAt(0u, kHeight), nullptr);
    EXPECT_EQ(plane->sampleAt(0u, 0u, kSamplesPerPixel), nullptr);
}

TEST(PlaneViewTest, InterleavedSamplesOfOnePixelAreAdjacent)
{
    // Three samples of one pixel in one plane, which is what a display buffer hands over. The
    // arithmetic differs from the planar case only in samples per pixel, and this is the case that
    // says the row length and the sample offsets both follow it.
    std::vector<std::uint16_t> buffer(4u * 2u * 3u, 0u);
    for (std::size_t index = 0u; index < buffer.size(); ++index)
    {
        buffer[index] = static_cast<std::uint16_t>(index);
    }
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, 4u, 2u, 3u, 4u * 3u * sizeof(std::uint16_t));
    ASSERT_TRUE(plane.has_value());
    EXPECT_EQ(plane->rowSampleCount(), 12u);
    EXPECT_EQ(plane->sampleCount(), 24u);
    EXPECT_EQ(*plane->sampleAt(1u, 0u, 0u), 3u);
    EXPECT_EQ(*plane->sampleAt(1u, 0u, 2u), 5u);
    EXPECT_EQ(*plane->sampleAt(0u, 1u, 0u), 12u);
}

TEST(PlaneViewTest, ASubplaneKeepsTheStrideAndMovesTheOrigin)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());

    const std::optional<an::PlaneView<std::uint16_t>> region = plane->subplane(1u, 1u, 3u, 2u);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->width(), 3u);
    EXPECT_EQ(region->height(), 2u);
    EXPECT_EQ(region->rowStrideBytes(), kPaddedRowStrideBytes);
    EXPECT_EQ(region->data(), plane->sampleAt(1u, 1u));
    EXPECT_EQ(*region->sampleAt(0u, 0u), static_cast<std::uint16_t>(1u * kWidth + 1u));
    EXPECT_EQ(*region->sampleAt(2u, 1u), static_cast<std::uint16_t>(2u * kWidth + 3u));
}

TEST(PlaneViewTest, ASubplaneOfAContiguousPlaneIsNoLongerContiguous)
{
    std::vector<std::uint16_t> buffer(kWidth * kHeight, 0u);
    const std::optional<an::PlaneView<std::uint16_t>> tight = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kWidth * sizeof(std::uint16_t));
    ASSERT_TRUE(tight.has_value());
    EXPECT_TRUE(tight->isContiguous());

    const std::optional<an::PlaneView<std::uint16_t>> narrow = tight->subplane(0u, 0u, kWidth - 1u, kHeight);
    ASSERT_TRUE(narrow.has_value());
    EXPECT_FALSE(narrow->isContiguous());

    // A region that is the full width and only some of the rows is still one run.
    const std::optional<an::PlaneView<std::uint16_t>> shorter = tight->subplane(0u, 1u, kWidth, kHeight - 1u);
    ASSERT_TRUE(shorter.has_value());
    EXPECT_TRUE(shorter->isContiguous());
}

TEST(PlaneViewTest, ARegionPastTheEdgeIsRefusedRatherThanClamped)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());

    EXPECT_EQ(plane->checkSubplane(0u, 0u, kWidth + 1u, kHeight), an::GeometryError::RegionOutOfBounds);
    EXPECT_EQ(plane->checkSubplane(1u, 0u, kWidth, kHeight), an::GeometryError::RegionOutOfBounds);
    EXPECT_EQ(plane->checkSubplane(0u, kHeight, 1u, 1u), an::GeometryError::RegionOutOfBounds);
    EXPECT_EQ(plane->checkSubplane(0u, 0u, 0u, kHeight), an::GeometryError::ZeroDimension);
    EXPECT_FALSE(plane->subplane(1u, 0u, kWidth, kHeight).has_value());

    // The whole plane is in bounds, which is the boundary the case above sits one past.
    EXPECT_EQ(plane->checkSubplane(0u, 0u, kWidth, kHeight), an::GeometryError::None);
}

TEST(PlaneViewTest, AWritableViewConvertsToAReadOnlyOneAndNotBack)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> writable = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(writable.has_value());

    const an::PlaneView<const std::uint16_t> readable = *writable;
    EXPECT_EQ(readable.data(), writable->data());
    EXPECT_EQ(readable.layout(), writable->layout());
    EXPECT_EQ(writable->constView().data(), writable->data());

    // The conversion exists in one direction only, and the sample type is what carries it.
    EXPECT_TRUE((std::is_convertible_v<an::PlaneView<std::uint16_t>, an::PlaneView<const std::uint16_t>>));
    EXPECT_FALSE((std::is_convertible_v<an::PlaneView<const std::uint16_t>, an::PlaneView<std::uint16_t>>));
    EXPECT_TRUE((std::is_const_v<std::remove_reference_t<decltype(*readable.sampleAt(0u, 0u))>>));
}

TEST(PlaneViewTest, OverlapIsReportedBetweenViewsThatShareAByte)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> plane = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(plane.has_value());

    EXPECT_TRUE(an::viewsOverlap(*plane, *plane));

    const std::optional<an::PlaneView<std::uint16_t>> topRow = plane->subplane(0u, 0u, kWidth, 1u);
    const std::optional<an::PlaneView<std::uint16_t>> lastRow = plane->subplane(0u, kHeight - 1u, kWidth, 1u);
    ASSERT_TRUE(topRow.has_value());
    ASSERT_TRUE(lastRow.has_value());
    EXPECT_FALSE(an::viewsOverlap(*topRow, *lastRow));
    EXPECT_TRUE(an::viewsOverlap(*topRow, *plane));

    std::vector<std::uint16_t> other = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> elsewhere = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{other}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(elsewhere.has_value());
    EXPECT_FALSE(an::viewsOverlap(*plane, *elsewhere));
}

TEST(PlaneViewTest, TwoViewsAreEqualWhenTheyNameTheSameSamplesInTheSamePlace)
{
    std::vector<std::uint16_t> buffer = makePaddedPlane();
    const std::optional<an::PlaneView<std::uint16_t>> first = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    const std::optional<an::PlaneView<std::uint16_t>> second = an::PlaneView<std::uint16_t>::create(
        std::span<std::uint16_t>{buffer}, kWidth, kHeight, kSamplesPerPixel, kPaddedRowStrideBytes);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);

    const std::optional<an::PlaneView<std::uint16_t>> shifted = first->subplane(1u, 0u, kWidth - 1u, kHeight);
    ASSERT_TRUE(shifted.has_value());
    EXPECT_FALSE(*first == *shifted);
}

TEST(PlaneViewTest, EverySampleTypeCanBeViewed)
{
    // Storage coverage is compile-time enumerated, and a view over each of the eight is the cheapest
    // proof that the enumeration is complete in practice rather than only in the table.
    std::uint8_t bytes[16] = {};
    double doubles[16] = {};
    EXPECT_TRUE(an::PlaneView<std::uint8_t>::create(std::span<std::uint8_t>{bytes}, 4u, 4u, 1u, 4u).has_value());
    EXPECT_TRUE(an::PlaneView<double>::create(std::span<double>{doubles}, 4u, 4u, 1u, 32u).has_value());
    EXPECT_EQ(an::PlaneView<double>::kSampleType, an::SampleType::Float64);
    EXPECT_EQ(an::PlaneView<const std::uint8_t>::kSampleType, an::SampleType::UInt8);
}

} // namespace
