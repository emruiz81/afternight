// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The display proxy: each pixel the mean of the valid samples of its block, returned to the image's representation; a
/// block with nothing valid invalid and every other block stated without a mask; a mosaic reduced as one grey plane;
/// and the cache that keeps proxies by the revision and factor they were reduced from.

#include "ControlsSessionTestSupport.h"

#include "session/DisplayProxy.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{

using an::tests::controls::assetOf;
using an::tests::controls::blockMean;
using an::ui::DisplayProxy;
using an::ui::DisplayProxyCache;

/// Reduces an asset under a fresh admitted operation.
[[nodiscard]] an::Result<std::shared_ptr<const DisplayProxy>>
reduce(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& asset, std::uint32_t factor)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return DisplayProxy::build(scope->engine(), scope->execution(), *lease, factor);
}

/// One sample of a proxy, of its planar layout.
template <typename SampleT>
[[nodiscard]] SampleT proxySample(const DisplayProxy& proxy, std::uint32_t channel, std::uint32_t x, std::uint32_t y)
{
    const std::optional<std::uint64_t> offset = proxy.layout().sampleByteOffset(channel, y, x, 0u);
    return *reinterpret_cast<const SampleT*>(static_cast<const std::uint8_t*>(proxy.samples()) + *offset);
}

} // namespace

TEST(DisplayProxyTest, APixelIsTheMeanOfItsBlockInTheImagesOwnRepresentationAndPartialEdgesAreLeftOut)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<std::uint16_t>((x * 7919u + y * 104729u + channel * 1299709u) % 65536u);
        };
    std::optional<an::consumer::ImageAsset> asset =
        assetOf<std::uint16_t>(session, 13u, 8u, an::ChannelSchema::Rgb, value);
    ASSERT_TRUE(asset.has_value());

    const an::Result<std::shared_ptr<const DisplayProxy>> proxy = reduce(session, *asset, 3u);
    ASSERT_TRUE(proxy.hasValue()) << proxy.status().message();
    EXPECT_EQ((*proxy)->factor(), 3u);
    EXPECT_EQ((*proxy)->source(), asset->revision()) << "a proxy is keyed by the revision it was reduced from";
    EXPECT_EQ((*proxy)->descriptor().extent().width(), 4u) << "thirteen columns hold four whole blocks of three";
    EXPECT_EQ((*proxy)->descriptor().extent().height(), 2u);
    EXPECT_EQ((*proxy)->descriptor().sampleType(), an::SampleType::UInt16);
    EXPECT_EQ((*proxy)->descriptor().channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ((*proxy)->validity().mask, nullptr) << "a proxy every block of which is valid states no mask";
    EXPECT_TRUE((*proxy)->validity().descriptor.isAllValid());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 4u; ++x)
            {
                EXPECT_EQ(proxySample<std::uint16_t>(**proxy, channel, x, y),
                          *blockMean<std::uint16_t>(value, {}, 3u, channel, x, y))
                    << channel << " " << x << " " << y;
            }
        }
    }
}

TEST(DisplayProxyTest, ABlockWithNothingValidIsInvalidInTheProxyAndAnotherIsTheMeanOfWhatIsValid)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return 0.01f * static_cast<float>(x + 10u * y); };
    // The block at the origin has nothing valid; the next one has one invalid sample of four.
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> valid =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return !(x < 2u && y < 2u) && !(x == 3u && y == 1u); };
    std::optional<an::consumer::ImageAsset> asset =
        assetOf<float>(session, 6u, 4u, an::ChannelSchema::Mono, value, valid);
    ASSERT_TRUE(asset.has_value());

    const an::Result<std::shared_ptr<const DisplayProxy>> proxy = reduce(session, *asset, 2u);
    ASSERT_TRUE(proxy.hasValue()) << proxy.status().message();
    const an::KernelValidityView validity = (*proxy)->validity();
    ASSERT_NE(validity.mask, nullptr) << "a block with nothing valid makes the proxy state a mask";
    EXPECT_EQ(validity.descriptor.rule(), an::ValidityRule::ExplicitMask);
    EXPECT_EQ(proxySample<float>(**proxy, 0u, 0u, 0u), 0.0f)
        << "a block with nothing valid is written nowhere, so its sample is the zero the proxy started as";
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            const std::optional<float> expected = blockMean<float>(value, valid, 2u, 0u, x, y);
            EXPECT_EQ(validity.mask->test(0u, x, y).value_or(true), expected.has_value()) << x << " " << y;
            if (expected.has_value())
            {
                EXPECT_EQ(proxySample<float>(**proxy, 0u, x, y), *expected) << x << " " << y;
            }
        }
    }
}

TEST(DisplayProxyTest, AMosaicIsReducedAsOneGreyPlaneEveryFilterOfABlockInItsMean)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            // Red, green and blue sites of an RGGB mosaic hold distinct levels.
            return static_cast<std::uint16_t>(x % 2u == 0u && y % 2u == 0u   ? 40000u
                                              : x % 2u == 1u && y % 2u == 1u ? 1000u
                                                                             : 9000u + x + y);
        };
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    std::optional<an::consumer::ImageAsset> mosaic =
        assetOf<std::uint16_t>(session, 8u, 6u, an::ChannelSchema::Mono, value, {}, *rggb);
    ASSERT_TRUE(mosaic.has_value());

    const an::Result<std::shared_ptr<const DisplayProxy>> proxy = reduce(session, *mosaic, 2u);
    ASSERT_TRUE(proxy.hasValue()) << proxy.status().message();
    EXPECT_FALSE((*proxy)->descriptor().isMosaiced()) << "the proxy of a mosaic is grey";
    EXPECT_EQ((*proxy)->descriptor().extent().width(), 4u);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_EQ(proxySample<std::uint16_t>(**proxy, 0u, x, y), *blockMean<std::uint16_t>(value, {}, 2u, 0u, x, y))
                << x << " " << y;
        }
    }
}

TEST(DisplayProxyTest, AFactorThatReducesNothingOrLeavesNoPixelIsRefused)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        assetOf<std::uint8_t>(session, 5u, 3u, an::ChannelSchema::Mono,
                              [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{9}; });
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(an::ui::largestDisplayProxyFactor(5u, 3u), 3u);
    for (const std::uint32_t factor : {0u, 1u, 4u})
    {
        const an::Result<std::shared_ptr<const DisplayProxy>> refused = reduce(session, *asset, factor);
        ASSERT_FALSE(refused.hasValue()) << factor;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::OutOfRange) << factor;
    }
    const an::Result<std::shared_ptr<const DisplayProxy>> largest = reduce(session, *asset, 3u);
    ASSERT_TRUE(largest.hasValue()) << largest.status().message();
    EXPECT_EQ((*largest)->descriptor().extent().width(), 1u);
    EXPECT_EQ((*largest)->descriptor().extent().height(), 1u);
}

TEST(DisplayProxyTest, TheCacheFindsAProxyByRevisionAndFactorAndLetsTheLeastRecentlyUsedGo)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const auto flat = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; };
    std::optional<an::consumer::ImageAsset> first = assetOf<float>(session, 12u, 12u, an::ChannelSchema::Mono, flat);
    std::optional<an::consumer::ImageAsset> second = assetOf<float>(session, 12u, 12u, an::ChannelSchema::Mono, flat);
    ASSERT_TRUE(first.has_value() && second.has_value());
    const an::Result<std::shared_ptr<const DisplayProxy>> byTwo = reduce(session, *first, 2u);
    const an::Result<std::shared_ptr<const DisplayProxy>> byThree = reduce(session, *first, 3u);
    const an::Result<std::shared_ptr<const DisplayProxy>> other = reduce(session, *second, 2u);
    ASSERT_TRUE(byTwo.hasValue() && byThree.hasValue() && other.hasValue());

    DisplayProxyCache cache(2u);
    cache.keep(*byTwo);
    cache.keep(*byThree);
    EXPECT_EQ(cache.find(first->revision(), 2u), *byTwo) << "found by the revision and the factor";
    EXPECT_EQ(cache.find(first->revision(), 4u), nullptr) << "another factor is another proxy";
    cache.keep(*other);
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.find(first->revision(), 3u), nullptr) << "the least recently used went first";
    EXPECT_EQ(cache.find(first->revision(), 2u), *byTwo);
    EXPECT_EQ(cache.find(second->revision(), 2u), *other);

    // A write makes another revision, under which the proxy of the old one is never found.
    {
        an::Result<an::consumer::AssetWriteLease> writer = first->write();
        ASSERT_TRUE(writer.hasValue());
        an::tests::controls::writeSample(*writer, 1u, 1u, 0.75f);
    }
    EXPECT_EQ(cache.find(first->revision(), 2u), nullptr);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
}
