// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/ImageView.h.
//
// An image view is a plane view with one more distance in it, and almost every case here is about
// what that distance makes possible to get wrong. A plane index that is not bounds-checked reads
// past the last plane; a plane offset computed from the row stride instead of the plane stride
// reads the first plane over and over; and a subview that crops one plane and not the others
// produces an image whose channels no longer describe the same pixels.
//
// The interleaved fixtures are here for a reason too. The same channel count is one plane of three
// samples or three planes of one, the arithmetic is the same with the numbers swapped, and a
// kernel written against only one of them is the defect that `PlaneView` being a separate type is
// meant to catch. These cases run both.

#include <AfterNightEngine/Image/ImageView.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace
{

/// The dimensions every fixture below uses.
constexpr std::uint32_t kWidth = 4u;
constexpr std::uint32_t kHeight = 3u;
constexpr std::uint32_t kChannelCount = 3u;

/// A validated layout, or a failed case.
///
/// @param samplesPerPixel how many of one pixel's samples share a plane.
/// @param planeCount how many planes there are.
/// @param rowAlignmentBytes the boundary rows and planes start on; 1 for none.
/// @return the layout.
[[nodiscard]] an::ImageLayout layoutOf(std::uint32_t samplesPerPixel, std::uint32_t planeCount,
                                       std::uint64_t rowAlignmentBytes = 1u)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(kWidth, kHeight, kChannelCount, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = extent.value(),
                                         .elementSizeBytes = static_cast<std::uint32_t>(sizeof(std::uint16_t)),
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = rowAlignmentBytes};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value();
}

/// A buffer large enough for a layout, with every sample set to its own byte offset.
///
/// Writing the offset rather than a counter is what makes an addressing case readable: an expected
/// value is the address the sample should have been at, so a wrong stride shows up as a number a
/// reader can subtract.
///
/// @param layout the geometry to size the buffer for.
/// @return the buffer.
[[nodiscard]] std::vector<std::uint16_t> makeBuffer(const an::ImageLayout& layout)
{
    std::vector<std::uint16_t> buffer(static_cast<std::size_t>(layout.totalBytes() / sizeof(std::uint16_t)), 0u);
    for (std::size_t index = 0u; index < buffer.size(); ++index)
    {
        buffer[index] = static_cast<std::uint16_t>(index * sizeof(std::uint16_t));
    }
    return buffer;
}

TEST(ImageViewTest, ALayoutOfAnotherElementSizeIsRefused)
{
    // The one rule that pairs an untyped layout with a typed pointer. Admitted, it is a run over
    // 16-bit samples reading half as many 32-bit ones out of the same bytes - which produces
    // plausible numbers and a wrong image.
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    std::vector<std::uint32_t> wider(buffer.size(), 0u);

    EXPECT_EQ(an::ImageView<std::uint16_t>::check(std::span<std::uint16_t>{buffer}, layout), an::GeometryError::None);
    EXPECT_EQ(an::ImageView<std::uint32_t>::check(std::span<std::uint32_t>{wider}, layout),
              an::GeometryError::ElementSizeMismatch);
    EXPECT_FALSE(an::ImageView<std::uint32_t>::create(std::span<std::uint32_t>{wider}, layout).has_value());
}

TEST(ImageViewTest, ABufferThatCannotCarryTheLayoutIsRefused)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);

    EXPECT_EQ(an::ImageView<std::uint16_t>::check(std::span<std::uint16_t>{buffer.data(), buffer.size() - 1u}, layout),
              an::GeometryError::BufferTooSmall);

    alignas(std::uint16_t) std::uint8_t raw[512] = {};
    void* const oddAddress = raw + 1;
    auto* const misaligned = static_cast<std::uint16_t*>(oddAddress);
    EXPECT_EQ(an::ImageView<std::uint16_t>::check(std::span<std::uint16_t>{misaligned, buffer.size()}, layout),
              an::GeometryError::BaseAddressMisaligned);
}

TEST(ImageViewTest, PlanesAreAddressedByThePlaneStrideAndNotTheRowStride)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->planeCount(), kChannelCount);
    EXPECT_EQ(an::storageArrangementOf(view->layout()), an::StorageArrangement::Planar);

    for (std::uint32_t planeIndex = 0u; planeIndex < kChannelCount; ++planeIndex)
    {
        const std::optional<an::PlaneView<std::uint16_t>> plane = view->plane(planeIndex);
        ASSERT_TRUE(plane.has_value()) << planeIndex;
        EXPECT_EQ(plane->width(), kWidth);
        EXPECT_EQ(plane->height(), kHeight);
        EXPECT_EQ(plane->samplesPerPixel(), 1u);
        // The first sample of each plane holds its own byte offset, which is the plane stride
        // multiplied out - the number a row-stride mistake would get wrong by a factor of the
        // height.
        EXPECT_EQ(*plane->sampleAt(0u, 0u), static_cast<std::uint16_t>(planeIndex * layout.planeStrideBytes()));
    }
    EXPECT_FALSE(view->plane(kChannelCount).has_value());
}

TEST(ImageViewTest, AnInterleavedImageHasOnePlaneHoldingEveryChannel)
{
    const an::ImageLayout layout = layoutOf(kChannelCount, 1u);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->planeCount(), 1u);
    EXPECT_EQ(view->samplesPerPixel(), kChannelCount);
    EXPECT_EQ(an::storageArrangementOf(view->layout()), an::StorageArrangement::Interleaved);

    const std::optional<an::PlaneView<std::uint16_t>> plane = view->plane(0u);
    ASSERT_TRUE(plane.has_value());
    EXPECT_EQ(plane->rowSampleCount(), static_cast<std::uint64_t>(kWidth) * kChannelCount);
    EXPECT_EQ(*view->sampleAt(0u, 1u, 0u, 2u), static_cast<std::uint16_t>((1u * kChannelCount + 2u) * 2u));
    EXPECT_FALSE(view->plane(1u).has_value());
}

TEST(ImageViewTest, ARowIsAddressedByPlaneThenRow)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount, 64u);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());
    EXPECT_FALSE(view->isContiguous());

    for (std::uint32_t planeIndex = 0u; planeIndex < kChannelCount; ++planeIndex)
    {
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            const std::optional<std::span<std::uint16_t>> row = view->row(planeIndex, y);
            ASSERT_TRUE(row.has_value());
            EXPECT_EQ(row->size(), kWidth);
            const std::uint64_t expected = planeIndex * layout.planeStrideBytes() + y * layout.rowStrideBytes();
            EXPECT_EQ((*row)[0], static_cast<std::uint16_t>(expected));
        }
    }
    EXPECT_FALSE(view->row(kChannelCount, 0u).has_value());
    EXPECT_FALSE(view->row(0u, kHeight).has_value());
}

TEST(ImageViewTest, OnlyAnImageWithNoPaddingAtAllIsOneRun)
{
    const an::ImageLayout tight = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(tight);
    const std::optional<an::ImageView<std::uint16_t>> whole =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, tight);
    ASSERT_TRUE(whole.has_value());
    EXPECT_TRUE(whole->isContiguous());
    const std::optional<std::span<std::uint16_t>> run = whole->contiguousSpan();
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run->size(), static_cast<std::size_t>(kWidth) * kHeight * kChannelCount);

    const an::ImageLayout padded = layoutOf(1u, kChannelCount, 64u);
    std::vector<std::uint16_t> paddedBuffer = makeBuffer(padded);
    const std::optional<an::ImageView<std::uint16_t>> spread =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{paddedBuffer}, padded);
    ASSERT_TRUE(spread.has_value());
    EXPECT_FALSE(spread->contiguousSpan().has_value());
}

TEST(ImageViewTest, SampleAccessIsBoundsCheckedInEveryIndex)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());

    EXPECT_NE(view->sampleAt(kChannelCount - 1u, kWidth - 1u, kHeight - 1u), nullptr);
    EXPECT_EQ(view->sampleAt(kChannelCount, 0u, 0u), nullptr);
    EXPECT_EQ(view->sampleAt(0u, kWidth, 0u), nullptr);
    EXPECT_EQ(view->sampleAt(0u, 0u, kHeight), nullptr);
    EXPECT_EQ(view->sampleAt(0u, 0u, 0u, 1u), nullptr);
}

TEST(ImageViewTest, ASubviewCropsEveryPlaneIdentically)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());

    const std::optional<an::ImageView<std::uint16_t>> region = view->subview(1u, 1u, 2u, 2u);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->width(), 2u);
    EXPECT_EQ(region->height(), 2u);
    EXPECT_EQ(region->channelCount(), kChannelCount);
    EXPECT_EQ(region->planeCount(), kChannelCount);
    EXPECT_EQ(region->rowStrideBytes(), layout.rowStrideBytes());
    EXPECT_EQ(region->planeStrideBytes(), layout.planeStrideBytes());

    // Every plane's region starts at the same pixel of that plane, which is what "identically"
    // means here and what a per-plane origin mistake would break for planes one and two only.
    for (std::uint32_t planeIndex = 0u; planeIndex < kChannelCount; ++planeIndex)
    {
        EXPECT_EQ(region->sampleAt(planeIndex, 0u, 0u), view->sampleAt(planeIndex, 1u, 1u)) << planeIndex;
    }
}

TEST(ImageViewTest, ARegionPastTheEdgeIsRefusedRatherThanClamped)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(view.has_value());

    EXPECT_EQ(view->checkSubview(0u, 0u, kWidth, kHeight), an::GeometryError::None);
    EXPECT_EQ(view->checkSubview(1u, 0u, kWidth, kHeight), an::GeometryError::RegionOutOfBounds);
    EXPECT_EQ(view->checkSubview(0u, 0u, kWidth + 1u, kHeight), an::GeometryError::RegionOutOfBounds);
    EXPECT_EQ(view->checkSubview(0u, 0u, kWidth, 0u), an::GeometryError::ZeroDimension);
    EXPECT_FALSE(view->subview(0u, kHeight, kWidth, 1u).has_value());
}

TEST(ImageViewTest, AWritableViewConvertsToAReadOnlyOneAndNotBack)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> writable =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    ASSERT_TRUE(writable.has_value());

    const an::ImageView<const std::uint16_t> readable = *writable;
    EXPECT_EQ(readable.data(), writable->data());
    EXPECT_EQ(readable.layout(), writable->layout());
    EXPECT_EQ(writable->constView().data(), writable->data());

    EXPECT_TRUE((std::is_convertible_v<an::ImageView<std::uint16_t>, an::ImageView<const std::uint16_t>>));
    EXPECT_FALSE((std::is_convertible_v<an::ImageView<const std::uint16_t>, an::ImageView<std::uint16_t>>));

    // A read-only image still hands out read-only planes, which is the half that a conversion
    // written per type rather than carried by the sample type would be free to get wrong.
    const std::optional<an::PlaneView<const std::uint16_t>> plane = readable.plane(0u);
    ASSERT_TRUE(plane.has_value());
    EXPECT_TRUE((std::is_const_v<std::remove_reference_t<decltype(*plane->sampleAt(0u, 0u))>>));
}

TEST(ImageViewTest, OverlapIsReportedBetweenViewsThatShareAByte)
{
    const an::ImageLayout layout = layoutOf(1u, kChannelCount);
    std::vector<std::uint16_t> buffer = makeBuffer(layout);
    std::vector<std::uint16_t> other = makeBuffer(layout);
    const std::optional<an::ImageView<std::uint16_t>> view =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{buffer}, layout);
    const std::optional<an::ImageView<std::uint16_t>> elsewhere =
        an::ImageView<std::uint16_t>::create(std::span<std::uint16_t>{other}, layout);
    ASSERT_TRUE(view.has_value());
    ASSERT_TRUE(elsewhere.has_value());

    EXPECT_TRUE(an::viewsOverlap(*view, *view));
    EXPECT_FALSE(an::viewsOverlap(*view, *elsewhere));

    const std::optional<an::ImageView<std::uint16_t>> region = view->subview(0u, 0u, 1u, 1u);
    ASSERT_TRUE(region.has_value());
    EXPECT_TRUE(an::viewsOverlap(*view, *region));
}

TEST(ImageViewTest, EverySampleTypeCanBeViewed)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(2u, 2u, 1u, limits);
    ASSERT_TRUE(extent.has_value());

    std::uint8_t bytes[4] = {};
    double doubles[4] = {};
    const an::ImageLayoutRequest byteRequest{
        .extent = *extent, .elementSizeBytes = 1u, .samplesPerPixel = 1u, .planeCount = 1u, .rowAlignmentBytes = 1u};
    const an::ImageLayoutRequest doubleRequest{
        .extent = *extent, .elementSizeBytes = 8u, .samplesPerPixel = 1u, .planeCount = 1u, .rowAlignmentBytes = 1u};

    EXPECT_TRUE(an::ImageView<std::uint8_t>::create(std::span<std::uint8_t>{bytes},
                                                    an::ImageLayout::create(byteRequest, limits).value())
                    .has_value());
    EXPECT_TRUE(an::ImageView<double>::create(std::span<double>{doubles},
                                              an::ImageLayout::create(doubleRequest, limits).value())
                    .has_value());
    EXPECT_EQ(an::ImageView<double>::kSampleType, an::SampleType::Float64);
}

} // namespace
