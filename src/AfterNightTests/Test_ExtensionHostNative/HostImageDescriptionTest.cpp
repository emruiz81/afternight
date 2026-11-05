// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the full host tells an extension about the image its native source view shows, when no shared buffer describes
/// it: the dimensions and the sample type, named in the protocol's vocabulary as every other `pixel_type` the host
/// sends is.

#include "HostImageDescription.h"
#include "HostNativeTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{

/// A zeroed grey image of a representation, as an asset of the run's session.
template <typename SampleT> std::optional<an::consumer::ImageAsset> zeroedGreyAsset(an::SampleType type)
{
    const an::consumer::EngineSession& session = an::tests::host_native::suiteSession();
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(7u, 5u, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = type,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, SampleT{});
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

struct Representation
{
    an::SampleType engine;
    std::function<std::optional<an::consumer::ImageAsset>()> make;
    const char* protocolName;
};

const Representation kRepresentations[] = {
    {an::SampleType::UInt8, [] { return zeroedGreyAsset<std::uint8_t>(an::SampleType::UInt8); }, "uint8"},
    {an::SampleType::Int8, [] { return zeroedGreyAsset<std::int8_t>(an::SampleType::Int8); }, ""},
    {an::SampleType::UInt16, [] { return zeroedGreyAsset<std::uint16_t>(an::SampleType::UInt16); }, "uint16"},
    {an::SampleType::Int16, [] { return zeroedGreyAsset<std::int16_t>(an::SampleType::Int16); }, ""},
    {an::SampleType::UInt32, [] { return zeroedGreyAsset<std::uint32_t>(an::SampleType::UInt32); }, ""},
    {an::SampleType::Int32, [] { return zeroedGreyAsset<std::int32_t>(an::SampleType::Int32); }, ""},
    {an::SampleType::Float32, [] { return zeroedGreyAsset<float>(an::SampleType::Float32); }, "float32"},
    {an::SampleType::Float64, [] { return zeroedGreyAsset<double>(an::SampleType::Float64); }, "float64"},
};

TEST(HostImageDescriptionTest, EverySampleTypeIsNamedInTheProtocolsWords)
{
    for (const Representation& representation : kRepresentations)
    {
        EXPECT_EQ(an::ui::extensions::protocolPixelTypeName(representation.engine), representation.protocolName)
            << an::sampleTypeName(representation.engine);
    }
}

TEST(HostImageDescriptionTest, TheSourceViewsImageIsDescribedAsABufferWouldBe)
{
    for (const Representation& representation : kRepresentations)
    {
        std::optional<an::consumer::ImageAsset> asset = representation.make();
        ASSERT_TRUE(asset.has_value()) << an::sampleTypeName(representation.engine);

        ImageViewControl view;
        view.initialize("described_source", ViewControlMode::st_preview);
        an::tests::host_native::showInView(view, std::move(*asset));

        const std::optional<an::ui::extensions::HostImageDimensions> dimensions =
            an::ui::extensions::viewImageDimensions(&view);
        ASSERT_TRUE(dimensions.has_value());
        EXPECT_EQ(dimensions->width, 7);
        EXPECT_EQ(dimensions->height, 5);
        EXPECT_EQ(dimensions->channels, 1);
        EXPECT_EQ(dimensions->pixelType, representation.protocolName) << an::sampleTypeName(representation.engine);
        EXPECT_EQ(dimensions->pixelType.find(' '), std::string::npos) << "no display string of another vocabulary";
    }
}

TEST(HostImageDescriptionTest, AViewShowingNothingDescribesNothing)
{
    ImageViewControl view;
    view.initialize("empty_source", ViewControlMode::st_preview);
    EXPECT_FALSE(an::ui::extensions::viewImageDimensions(&view).has_value());
    EXPECT_FALSE(an::ui::extensions::viewImageDimensions(nullptr).has_value());
}

} // namespace
