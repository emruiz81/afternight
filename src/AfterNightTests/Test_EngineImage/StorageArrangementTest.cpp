// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Image/StorageArrangement.h.
//
// ImageLayout deliberately refuses to name the arrangement it describes: it holds samples per
// plane and a plane count, which is the arithmetic, and leaves the word to the image model. This
// header is where the word is assigned, so these cases are about the boundary between the two
// spellings staying exact - especially for a single-channel image, where planar and interleaved are
// the same bytes and only one of them can be reported.

#include <AfterNightEngine/Image/StorageArrangement.h>

#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>

namespace
{

an::ImageExtent extentOf(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channelCount, limits);
    EXPECT_TRUE(extent.has_value());
    return extent.value_or(an::ImageExtent::create(1u, 1u, 1u, limits).value());
}

an::ImageLayout layoutOf(std::uint32_t channelCount, std::uint32_t samplesPerPixel, std::uint32_t planeCount)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request{.extent = extentOf(64u, 32u, channelCount),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = samplesPerPixel,
                                         .planeCount = planeCount,
                                         .rowAlignmentBytes = 1u};
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(request, limits);
    EXPECT_TRUE(layout.has_value());
    return layout.value_or(an::ImageLayout::create({.extent = extentOf(1u, 1u, 1u),
                                                    .elementSizeBytes = 4u,
                                                    .samplesPerPixel = 1u,
                                                    .planeCount = 1u,
                                                    .rowAlignmentBytes = 1u},
                                                   limits)
                               .value());
}

TEST(StorageArrangementTest, EveryArrangementIsRecognisedAndNamedDistinctly)
{
    constexpr an::StorageArrangement kAll[] = {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved,
                                               an::StorageArrangement::SemiPlanar};
    for (const an::StorageArrangement left : kAll)
    {
        EXPECT_TRUE(an::isStorageArrangement(left));
        for (const an::StorageArrangement right : kAll)
        {
            const bool sameName = std::strcmp(an::storageArrangementName(left), an::storageArrangementName(right)) == 0;
            EXPECT_EQ(sameName, left == right);
        }
    }
    EXPECT_FALSE(an::isStorageArrangement(static_cast<an::StorageArrangement>(70)));
    EXPECT_STREQ(an::storageArrangementName(static_cast<an::StorageArrangement>(70)), "UnknownStorageArrangement");
}

TEST(StorageArrangementTest, OnePlanePerChannelIsPlanar)
{
    EXPECT_EQ(an::storageArrangementOf(layoutOf(3u, 1u, 3u)), an::StorageArrangement::Planar);
    EXPECT_EQ(an::storageArrangementOf(layoutOf(4u, 1u, 4u)), an::StorageArrangement::Planar);
}

TEST(StorageArrangementTest, OnePlaneHoldingEveryChannelIsInterleaved)
{
    EXPECT_EQ(an::storageArrangementOf(layoutOf(3u, 3u, 1u)), an::StorageArrangement::Interleaved);
    EXPECT_EQ(an::storageArrangementOf(layoutOf(4u, 4u, 1u)), an::StorageArrangement::Interleaved);
}

TEST(StorageArrangementTest, AnythingBetweenTheTwoIsSemiPlanar)
{
    // Four channels as two planes of two samples: neither one plane per channel nor one plane for
    // all of them, and the arrangement a luminance plane beside a chroma plane produces.
    EXPECT_EQ(an::storageArrangementOf(layoutOf(4u, 2u, 2u)), an::StorageArrangement::SemiPlanar);
    EXPECT_EQ(an::storageArrangementOf(layoutOf(6u, 3u, 2u)), an::StorageArrangement::SemiPlanar);
    EXPECT_EQ(an::storageArrangementOf(layoutOf(6u, 2u, 3u)), an::StorageArrangement::SemiPlanar);
}

TEST(StorageArrangementTest, ASingleChannelImageIsReportedPlanarBecauseTheTwoCoincide)
{
    // One channel in one plane satisfies both definitions. Reporting planar is a choice, and the
    // reason it is this one is that every multi-channel image in this engine that is not
    // interleaved is planar, so a caller comparing arrangements across a channel-count change sees
    // no spurious difference.
    EXPECT_EQ(an::storageArrangementOf(layoutOf(1u, 1u, 1u)), an::StorageArrangement::Planar);
}

TEST(StorageArrangementTest, PaddingBetweenRowsOrPlanesDoesNotChangeTheArrangement)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageLayoutRequest request{.extent = extentOf(63u, 32u, 3u),
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = 3u,
                                         .rowAlignmentBytes = 64u};
    const std::optional<an::ImageLayout> padded = an::ImageLayout::create(request, limits);
    ASSERT_TRUE(padded.has_value());
    EXPECT_FALSE(padded->isContiguous());
    EXPECT_EQ(an::storageArrangementOf(*padded), an::StorageArrangement::Planar);
}

TEST(StorageArrangementTest, TheArrangementIsAvailableAtCompileTime)
{
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    constexpr an::ImageLayoutRequest kRequest{.extent = an::ImageExtent::create(8u, 8u, 3u, kLimits).value(),
                                              .elementSizeBytes = 2u,
                                              .samplesPerPixel = 3u,
                                              .planeCount = 1u,
                                              .rowAlignmentBytes = 1u};
    static_assert(an::storageArrangementOf(an::ImageLayout::create(kRequest, kLimits).value()) ==
                  an::StorageArrangement::Interleaved);
    SUCCEED();
}

/// The channel schema an image of this many channels has, for the fixtures below.
///
/// @param channelCount one, three or four.
/// @return the schema whose fixed channel count is this one.
[[nodiscard]] an::ChannelSchema schemaFor(std::uint32_t channelCount)
{
    if (channelCount == 1u)
    {
        return an::ChannelSchema::Mono;
    }
    return channelCount == 4u ? an::ChannelSchema::Rgba : an::ChannelSchema::Rgb;
}

/// A descriptor for a frame of this many channels.
///
/// @param channelCount how many channels, which decides the schema.
/// @param sampleType how one sample is represented.
/// @return the descriptor.
[[nodiscard]] an::ImageDescriptor descriptorOf(std::uint32_t channelCount,
                                               an::SampleType sampleType = an::SampleType::UInt16)
{
    const an::ImageDescriptorRequest request{.extent = extentOf(64u, 32u, channelCount),
                                             .sampleType = sampleType,
                                             .channelSchema = schemaFor(channelCount),
                                             .colorSpace = channelCount == 1u ? an::ColorSpace::Unspecified
                                                                              : an::ColorSpace::LinearRgb,
                                             .numericDomain = an::NumericDomain::NormalizedLinear,
                                             .cfa = an::CfaDescriptor{},
                                             .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                             .pixelAspectRatio = 1.0};
    const std::optional<an::ImageDescriptor> descriptor = an::ImageDescriptor::create(request);
    EXPECT_TRUE(descriptor.has_value());
    return descriptor.value_or(an::ImageDescriptor::create({.extent = extentOf(1u, 1u, 1u),
                                                            .sampleType = an::SampleType::UInt16,
                                                            .channelSchema = an::ChannelSchema::Mono,
                                                            .colorSpace = an::ColorSpace::Unspecified,
                                                            .numericDomain = an::NumericDomain::NormalizedLinear,
                                                            .cfa = an::CfaDescriptor{},
                                                            .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                            .pixelAspectRatio = 1.0})
                                   .value());
}

TEST(StorageArrangementTest, TheGeometryDerivedFromADescriptionReportsTheArrangementItWasAskedFor)
{
    // The round trip is the property worth having: name an arrangement, derive the geometry, and
    // the geometry reports the same word back. A derivation that swapped the two numbers would
    // still produce a valid layout, and this is what refuses it.
    const an::EngineLimits limits = an::defaultEngineLimits();
    const an::ImageDescriptor rgb = descriptorOf(3u);

    for (const an::StorageArrangement arrangement :
         {an::StorageArrangement::Planar, an::StorageArrangement::Interleaved})
    {
        const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(rgb, arrangement);
        ASSERT_TRUE(request.has_value()) << an::storageArrangementName(arrangement);
        const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, limits);
        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(an::storageArrangementOf(*layout), arrangement);
        EXPECT_EQ(layout->elementSizeBytes(), an::sampleTypeSizeBytes(rgb.sampleType()));
        EXPECT_EQ(layout->extent(), rgb.extent());
        EXPECT_TRUE(an::layoutDescribes(rgb, *layout));
    }
}

TEST(StorageArrangementTest, ThePlaneCountAndSamplesPerPixelFollowTheArrangement)
{
    const an::ImageDescriptor rgb = descriptorOf(3u);

    const std::optional<an::ImageLayoutRequest> planar = an::layoutRequestFor(rgb, an::StorageArrangement::Planar);
    ASSERT_TRUE(planar.has_value());
    EXPECT_EQ(planar->samplesPerPixel, 1u);
    EXPECT_EQ(planar->planeCount, 3u);

    const std::optional<an::ImageLayoutRequest> interleaved =
        an::layoutRequestFor(rgb, an::StorageArrangement::Interleaved);
    ASSERT_TRUE(interleaved.has_value());
    EXPECT_EQ(interleaved->samplesPerPixel, 3u);
    EXPECT_EQ(interleaved->planeCount, 1u);

    // The alignment is the caller's, and the sample type decides the element size rather than the
    // other way round.
    const std::optional<an::ImageLayoutRequest> tight =
        an::layoutRequestFor(descriptorOf(1u, an::SampleType::Float64), an::StorageArrangement::Planar, 1u);
    ASSERT_TRUE(tight.has_value());
    EXPECT_EQ(tight->rowAlignmentBytes, 1u);
    EXPECT_EQ(tight->elementSizeBytes, 8u);
}

TEST(StorageArrangementTest, ASemiPlanarArrangementIsRefusedRatherThanGuessed)
{
    // Four channels split one-and-three and two-and-two are both semi-planar, the channel count
    // does not say which, and a descriptor has nowhere to state the split. Guessing would produce a
    // valid layout that describes an image nobody asked for.
    const an::ImageDescriptor rgba = descriptorOf(4u);
    EXPECT_FALSE(an::layoutRequestFor(rgba, an::StorageArrangement::SemiPlanar).has_value());
    EXPECT_FALSE(an::layoutRequestFor(rgba, static_cast<an::StorageArrangement>(70)).has_value());
}

TEST(StorageArrangementTest, ALayoutAndADescriptionAgreeOnTheTwoFactsTheyShare)
{
    const an::ImageDescriptor rgb = descriptorOf(3u);
    const std::optional<an::ImageLayoutRequest> request = an::layoutRequestFor(rgb, an::StorageArrangement::Planar);
    ASSERT_TRUE(request.has_value());
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    ASSERT_TRUE(layout.has_value());
    EXPECT_TRUE(an::layoutDescribes(rgb, *layout));

    // A different channel count is a different image, whatever the memory looks like.
    EXPECT_FALSE(an::layoutDescribes(descriptorOf(1u), *layout));

    // And so is a different sample size, which is the fact a typed view would otherwise get wrong.
    const an::ImageDescriptor wider = descriptorOf(3u, an::SampleType::Float32);
    EXPECT_FALSE(an::layoutDescribes(wider, *layout));
}

TEST(StorageArrangementTest, TheDerivationIsAvailableAtCompileTime)
{
    constexpr an::EngineLimits kLimits = an::defaultEngineLimits();
    constexpr an::ImageDescriptor kDescriptor =
        an::ImageDescriptor::create({.extent = an::ImageExtent::create(8u, 8u, 3u, kLimits).value(),
                                     .sampleType = an::SampleType::UInt16,
                                     .channelSchema = an::ChannelSchema::Rgb,
                                     .colorSpace = an::ColorSpace::LinearRgb,
                                     .numericDomain = an::NumericDomain::NormalizedLinear,
                                     .cfa = an::CfaDescriptor{},
                                     .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                     .pixelAspectRatio = 1.0})
            .value();
    static_assert(an::layoutRequestFor(kDescriptor, an::StorageArrangement::Interleaved)->planeCount == 1u);
    static_assert(an::layoutDescribes(
        kDescriptor,
        an::ImageLayout::create(an::layoutRequestFor(kDescriptor, an::StorageArrangement::Planar).value(), kLimits)
            .value()));
    SUCCEED();
}

} // namespace
