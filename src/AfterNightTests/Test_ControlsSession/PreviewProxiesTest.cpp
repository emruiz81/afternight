// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an interactive frame reads instead of its target: the target reduced - each pixel the mean of the valid samples
/// of its block, with the target's description over the reduced extent and its validity, a mask reduced with it - and
/// sampled one pixel per block, never averaged; every code of an eight- or sixteen-bit target in a row; the factor a
/// histogram's sample is taken at; and the cache that keeps each by the revision, kind and factor it was made from, so
/// a change of the target or of its mask never finds a stale one.

#include "ControlsSessionTestSupport.h"

#include "session/PreviewProxies.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

using an::tests::controls::assetOf;
using an::tests::controls::blockMean;
using an::ui::PreviewProxyCache;
using an::ui::PreviewProxyKind;

/// What a case makes of an asset.
enum class Made
{
    Reduced,
    Sampled,
    Codes
};

/// Makes a reduction, a sample or a row of codes of an asset under a fresh admitted operation.
[[nodiscard]] an::Result<an::consumer::ImageAsset> make(const an::consumer::EngineSession& session,
                                                        const an::consumer::ImageAsset& asset, Made made,
                                                        std::uint32_t factor = 1u)
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
    switch (made)
    {
    case Made::Reduced:
        return an::ui::reduceForPreview(session, scope->engine(), scope->execution(), *lease, factor);
    case Made::Sampled:
        return an::ui::sampleForPreview(session, scope->engine(), scope->execution(), *lease, factor);
    case Made::Codes:
        return an::ui::codeRampOf(session, scope->engine(), *lease);
    }
    return an::Status::failure(an::ErrorCode::Internal, "nothing was asked for");
}

/// One sample of an asset of `SampleT`, read under a lease.
template <typename SampleT>
[[nodiscard]] SampleT sampleOf(const an::consumer::ImageAsset& asset, std::uint32_t channel, std::uint32_t x,
                               std::uint32_t y)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue());
    const auto* typed = std::get_if<an::Image<SampleT>>(&lease->image());
    EXPECT_NE(typed, nullptr) << "the asset holds another representation";
    if (typed == nullptr)
    {
        return SampleT{};
    }
    const auto* sample = typed->view()->sampleAt(channel, x, y, 0u);
    EXPECT_NE(sample, nullptr);
    return sample != nullptr ? *sample : SampleT{};
}

/// Whether one pixel of an asset's mask is valid; nothing when the asset has no mask.
[[nodiscard]] std::optional<bool> maskBit(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue());
    const an::ValidityMask* validity = lease->validity();
    if (validity == nullptr)
    {
        return std::nullopt;
    }
    return validity->view()->test(0u, x, y);
}

/// The validity rule an asset's metadata states.
[[nodiscard]] an::ValidityRule ruleOf(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue());
    return lease->visit([](const auto& typed) { return typed.metadata().semantics().validity().rule(); });
}

/// Writes one sample of a single-channel Float32 asset, so it publishes another revision.
void touch(an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetWriteLease> writer = asset.write();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    an::tests::controls::writeSample(*writer, 0u, 0u, 0.75f);
}

} // namespace

TEST(PreviewProxiesTest, AReductionIsTheTargetsBlockMeansWithItsDescriptionOverTheReducedExtentAndAnIdentityOfItsOwn)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<std::uint16_t>((x * 7919u + y * 104729u + channel * 1299709u) % 65536u);
        };
    std::optional<an::consumer::ImageAsset> target =
        assetOf<std::uint16_t>(session, 13u, 8u, an::ChannelSchema::Rgb, value);
    ASSERT_TRUE(target.has_value());

    an::Result<an::consumer::ImageAsset> reduced = make(session, *target, Made::Reduced, 4u);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    const std::optional<an::ImageDescriptor> described = reduced->descriptor();
    ASSERT_TRUE(described.has_value());
    // The partial blocks at the right and the bottom are left out: 13 by 8 reduced by 4 is 3 by 2.
    EXPECT_EQ(described->extent().width(), 3u);
    EXPECT_EQ(described->extent().height(), 2u);
    EXPECT_EQ(described->extent().channelCount(), 3u);
    EXPECT_EQ(described->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(described->channelSchema(), an::ChannelSchema::Rgb);
    EXPECT_EQ(described->numericDomain(), target->descriptor()->numericDomain());
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 3u; ++x)
            {
                EXPECT_EQ(sampleOf<std::uint16_t>(*reduced, channel, x, y),
                          *blockMean<std::uint16_t>(value, {}, 4u, channel, x, y))
                    << "channel " << channel << " block " << x << ", " << y;
            }
        }
    }
    EXPECT_NE(reduced->identity(), target->identity());
    EXPECT_NE(reduced->revision(), target->revision()) << "a reduction's revision is never the target's";
    EXPECT_FALSE(maskBit(*reduced, 0u, 0u).has_value()) << "an image every sample of which is valid states no mask";
}

TEST(PreviewProxiesTest, AReductionOfAMaskedTargetIsMaskedWhereABlockHadNothingValidAndTheMeanOfWhatIsValidElsewhere)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return 0.01f * static_cast<float>(x + 10u * y); };
    // The block at the origin has nothing valid; the next one has one invalid sample of four.
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> valid =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return !(x < 2u && y < 2u) && !(x == 3u && y == 1u); };
    std::optional<an::consumer::ImageAsset> target =
        assetOf<float>(session, 6u, 4u, an::ChannelSchema::Mono, value, valid);
    ASSERT_TRUE(target.has_value());

    an::Result<an::consumer::ImageAsset> reduced = make(session, *target, Made::Reduced, 2u);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_EQ(ruleOf(*reduced), an::ValidityRule::ExplicitMask) << "the target's validity rule is kept";
    EXPECT_EQ(maskBit(*reduced, 0u, 0u), std::optional<bool>(false));
    EXPECT_EQ(maskBit(*reduced, 1u, 0u), std::optional<bool>(true));
    EXPECT_EQ(maskBit(*reduced, 2u, 1u), std::optional<bool>(true));
    EXPECT_EQ(sampleOf<float>(*reduced, 0u, 1u, 0u), *blockMean<float>(value, valid, 2u, 0u, 1u, 0u));
    EXPECT_EQ(sampleOf<float>(*reduced, 0u, 2u, 1u), *blockMean<float>(value, valid, 2u, 0u, 2u, 1u));
}

TEST(PreviewProxiesTest, ASampleIsTheTopLeftPixelOfEveryBlockAsItIsNeverAveraged)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<std::uint16_t>((x * 7919u + y * 104729u + channel * 1299709u) % 65536u);
        };
    std::optional<an::consumer::ImageAsset> target =
        assetOf<std::uint16_t>(session, 13u, 8u, an::ChannelSchema::Rgb, value);
    ASSERT_TRUE(target.has_value());

    an::Result<an::consumer::ImageAsset> sampled = make(session, *target, Made::Sampled, 3u);
    ASSERT_TRUE(sampled.hasValue()) << sampled.status().message();
    const std::optional<an::ImageDescriptor> described = sampled->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 4u);
    EXPECT_EQ(described->extent().height(), 2u);
    EXPECT_EQ(described->sampleType(), an::SampleType::UInt16);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            for (std::uint32_t x = 0u; x < 4u; ++x)
            {
                EXPECT_EQ(sampleOf<std::uint16_t>(*sampled, channel, x, y), value(3u * x, 3u * y, channel))
                    << "channel " << channel << " pixel " << x << ", " << y;
            }
        }
    }
    EXPECT_NE(sampled->revision(), target->revision()) << "a sample's revision is never the target's";
}

TEST(PreviewProxiesTest, ASampleOfAMaskedTargetTakesTheMaskOfThePixelsItTakes)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return 0.01f * static_cast<float>(x + 10u * y); };
    // Every pixel the sample takes is invalid where both its coordinates are a multiple of four.
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> valid =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return x % 4u != 0u || y % 4u != 0u; };
    std::optional<an::consumer::ImageAsset> target =
        assetOf<float>(session, 8u, 8u, an::ChannelSchema::Mono, value, valid);
    ASSERT_TRUE(target.has_value());

    an::Result<an::consumer::ImageAsset> sampled = make(session, *target, Made::Sampled, 2u);
    ASSERT_TRUE(sampled.hasValue()) << sampled.status().message();
    EXPECT_EQ(ruleOf(*sampled), an::ValidityRule::ExplicitMask);
    for (std::uint32_t y = 0u; y < 4u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            EXPECT_EQ(maskBit(*sampled, x, y), std::optional<bool>(valid(2u * x, 2u * y, 0u))) << x << ", " << y;
            EXPECT_EQ(sampleOf<float>(*sampled, 0u, x, y), value(2u * x, 2u * y, 0u)) << x << ", " << y;
        }
    }
}

TEST(PreviewProxiesTest, ARowOfCodesHoldsEveryCodeInOrderInEveryChannelOfTheTarget)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> unsignedTarget = assetOf<std::uint16_t>(
        session, 4u, 3u, an::ChannelSchema::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<std::uint16_t>(x + y); });
    ASSERT_TRUE(unsignedTarget.has_value());
    an::Result<an::consumer::ImageAsset> codes = make(session, *unsignedTarget, Made::Codes);
    ASSERT_TRUE(codes.hasValue()) << codes.status().message();
    const std::optional<an::ImageDescriptor> described = codes->descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 65536u);
    EXPECT_EQ(described->extent().height(), 1u);
    EXPECT_EQ(described->extent().channelCount(), 3u);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        for (const std::uint32_t code : {0u, 1u, 32767u, 32768u, 65534u, 65535u})
        {
            EXPECT_EQ(sampleOf<std::uint16_t>(*codes, channel, code, 0u), code) << "channel " << channel;
        }
    }

    std::optional<an::consumer::ImageAsset> signedTarget =
        assetOf<std::int8_t>(session, 4u, 3u, an::ChannelSchema::Mono,
                             [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<std::int8_t>(x); });
    ASSERT_TRUE(signedTarget.has_value());
    an::Result<an::consumer::ImageAsset> signedCodes = make(session, *signedTarget, Made::Codes);
    ASSERT_TRUE(signedCodes.hasValue()) << signedCodes.status().message();
    EXPECT_EQ(signedCodes->descriptor()->extent().width(), 256u);
    EXPECT_EQ(sampleOf<std::int8_t>(*signedCodes, 0u, 0u, 0u), std::int8_t{-128});
    EXPECT_EQ(sampleOf<std::int8_t>(*signedCodes, 0u, 128u, 0u), std::int8_t{0});
    EXPECT_EQ(sampleOf<std::int8_t>(*signedCodes, 0u, 255u, 0u), std::int8_t{127});
}

TEST(PreviewProxiesTest, AFloatingTargetHasNoRowOfCodesAndAMosaicIsNeitherReducedNorSampled)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> floating = assetOf<float>(
        session, 4u, 4u, an::ChannelSchema::Mono, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(floating.has_value());
    EXPECT_EQ(make(session, *floating, Made::Codes).status().code(), an::ErrorCode::Unsupported);

    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    std::optional<an::consumer::ImageAsset> mosaic = assetOf<std::uint16_t>(
        session, 8u, 8u, an::ChannelSchema::Mono,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<std::uint16_t>(x * 100u + y); }, {},
        *rggb);
    ASSERT_TRUE(mosaic.has_value());
    EXPECT_EQ(make(session, *mosaic, Made::Reduced, 2u).status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(make(session, *mosaic, Made::Sampled, 2u).status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(make(session, *mosaic, Made::Codes).status().code(), an::ErrorCode::Unsupported);
}

TEST(PreviewProxiesTest, AReductionIsByAtLeastTwoAndASampleLeavesAPixelEachWay)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> target = assetOf<float>(
        session, 6u, 4u, an::ChannelSchema::Mono, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(make(session, *target, Made::Reduced, 1u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(make(session, *target, Made::Reduced, 5u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(make(session, *target, Made::Sampled, 5u).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_TRUE(make(session, *target, Made::Sampled, 1u).hasValue()) << "a sample of every pixel is the target's";
}

TEST(PreviewProxiesTest, AHistogramsSampleIsTakenAtTheLargestFactorThatLeavesAMillionPixels)
{
    // The reference frame of the measurements, 6016 by 4016, leaves 1.5 million pixels at four and 0.97 at five.
    EXPECT_EQ(an::ui::previewSampleFactor(6016u, 4016u), 4u);
    EXPECT_EQ(an::ui::previewSampleFactor(1000u, 1000u), 1u);
    EXPECT_EQ(an::ui::previewSampleFactor(2000u, 2000u), 2u);
    // At two this one leaves 999,000 pixels, fewer than a million.
    EXPECT_EQ(an::ui::previewSampleFactor(2000u, 1999u), 1u);
    EXPECT_EQ(an::ui::previewSampleFactor(0u, 100u), 1u);
}

TEST(PreviewProxyCacheTest, AProxyIsFoundUnderTheRevisionKindAndFactorItWasKeptUnderAndNoOther)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> target = an::tests::controls::filledAsset(session, 0.25f, 8u, 8u);
    ASSERT_TRUE(target.has_value());
    an::Result<an::consumer::ImageAsset> reduced = make(session, *target, Made::Reduced, 4u);
    ASSERT_TRUE(reduced.hasValue());
    auto kept = std::make_shared<const an::consumer::ImageAsset>(std::move(*reduced));

    PreviewProxyCache cache;
    cache.keep(target->revision(), PreviewProxyKind::Reduced, 4u, kept);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Reduced, 4u), kept);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Reduced, 2u), nullptr);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Sampled, 4u), nullptr);
    EXPECT_EQ(cache.find(kept->revision(), PreviewProxyKind::Reduced, 4u), nullptr);
    cache.keep(target->revision(), PreviewProxyKind::Reduced, 4u, nullptr);
    EXPECT_EQ(cache.size(), 1u) << "nothing is kept for a null proxy";
}

TEST(PreviewProxyCacheTest, ATargetChangedOutsideTheDialogFindsNoneOfItsEntriesAndAChangedMaskLosesOnlyItsOwn)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> target = an::tests::controls::filledAsset(session, 0.25f, 8u, 8u);
    std::optional<an::consumer::ImageAsset> mask = an::tests::controls::filledAsset(session, 1.0f, 8u, 8u);
    ASSERT_TRUE(target.has_value() && mask.has_value());
    const auto proxyOf = [&session](const an::consumer::ImageAsset& asset, Made made, std::uint32_t factor) {
        an::Result<an::consumer::ImageAsset> proxy = make(session, asset, made, factor);
        EXPECT_TRUE(proxy.hasValue());
        return proxy.hasValue() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*proxy)) : nullptr;
    };

    PreviewProxyCache cache;
    const an::consumer::AssetRevision targetBefore = target->revision();
    const an::consumer::AssetRevision maskBefore = mask->revision();
    cache.keep(targetBefore, PreviewProxyKind::Reduced, 4u, proxyOf(*target, Made::Reduced, 4u));
    cache.keep(targetBefore, PreviewProxyKind::Sampled, 2u, proxyOf(*target, Made::Sampled, 2u));
    cache.keep(maskBefore, PreviewProxyKind::Reduced, 4u, proxyOf(*mask, Made::Reduced, 4u));

    // Another view's edit, an undo or a reload: the target's revision moves, and nothing kept of it is found for it.
    touch(*target);
    ASSERT_NE(target->revision(), targetBefore);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Reduced, 4u), nullptr);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Sampled, 2u), nullptr);
    EXPECT_NE(cache.find(mask->revision(), PreviewProxyKind::Reduced, 4u), nullptr) << "the mask did not change";

    // A mask edit: the mask's entry is lost, and nothing else was left to lose.
    touch(*mask);
    ASSERT_NE(mask->revision(), maskBefore);
    EXPECT_EQ(cache.find(mask->revision(), PreviewProxyKind::Reduced, 4u), nullptr);
}

TEST(PreviewProxyCacheTest, TheLeastRecentlyUsedEntryIsLetGoBeyondTheCapacity)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> target = an::tests::controls::filledAsset(session, 0.25f, 8u, 8u);
    ASSERT_TRUE(target.has_value());
    an::Result<an::consumer::ImageAsset> one = make(session, *target, Made::Reduced, 2u);
    an::Result<an::consumer::ImageAsset> two = make(session, *target, Made::Reduced, 4u);
    an::Result<an::consumer::ImageAsset> three = make(session, *target, Made::Reduced, 8u);
    ASSERT_TRUE(one.hasValue() && two.hasValue() && three.hasValue());

    PreviewProxyCache cache(2u);
    cache.keep(target->revision(), PreviewProxyKind::Reduced, 2u,
               std::make_shared<const an::consumer::ImageAsset>(std::move(*one)));
    cache.keep(target->revision(), PreviewProxyKind::Reduced, 4u,
               std::make_shared<const an::consumer::ImageAsset>(std::move(*two)));
    // Found, so the most recently used: the one at four goes when a third is kept.
    EXPECT_NE(cache.find(target->revision(), PreviewProxyKind::Reduced, 2u), nullptr);
    cache.keep(target->revision(), PreviewProxyKind::Reduced, 8u,
               std::make_shared<const an::consumer::ImageAsset>(std::move(*three)));
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_NE(cache.find(target->revision(), PreviewProxyKind::Reduced, 2u), nullptr);
    EXPECT_EQ(cache.find(target->revision(), PreviewProxyKind::Reduced, 4u), nullptr);
    EXPECT_NE(cache.find(target->revision(), PreviewProxyKind::Reduced, 8u), nullptr);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
}

namespace
{

/// A planar Float32 asset of one channel under the finite-only rule, each sample `value(x, y, 0)`, which may be
/// not-a-number; null when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
finiteOnlyAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
                const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, hold->engine().limits());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(
        an::ImageSemanticsRequest{.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})});
    EXPECT_TRUE(descriptor.has_value() && semantics.hasValue());
    if (!descriptor.has_value() || !semantics.hasValue())
    {
        return std::nullopt;
    }
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue());
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = value(x, y, 0u);
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    EXPECT_TRUE(asset.hasValue());
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

} // namespace

// A target under a value rule may have a block with nothing valid - a stack's region of not-a-number under the
// finite-only rule - whose mean is written nowhere: a zero there would read as valid under the target's rule, so the
// reduction states a mask, which says it; a target every block of which has a valid sample keeps its rule and carries
// none.
TEST(PreviewProxiesTest, AReductionOfATargetUnderAValueRuleStatesAMaskWhereABlockHadNothingValid)
{
    std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::controls::openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    constexpr float kNotANumber = std::numeric_limits<float>::quiet_NaN();

    // The block at the origin holds nothing finite; the next one has one not-a-number of four.
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> finite =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return !(x < 2u && y < 2u) && !(x == 3u && y == 1u); };
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> value =
        [finite](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return finite(x, y, c) ? 0.01f * static_cast<float>(x + 10u * y) : kNotANumber;
        };
    std::optional<an::consumer::ImageAsset> target = finiteOnlyAsset(session, 6u, 4u, value);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(ruleOf(*target), an::ValidityRule::FiniteOnly);

    an::Result<an::consumer::ImageAsset> reduced = make(session, *target, Made::Reduced, 2u);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_EQ(ruleOf(*reduced), an::ValidityRule::ExplicitMask)
        << "a zero where a block had nothing finite would read as valid under the target's rule";
    EXPECT_EQ(maskBit(*reduced, 0u, 0u), std::optional<bool>(false));
    EXPECT_EQ(maskBit(*reduced, 1u, 0u), std::optional<bool>(true));
    EXPECT_EQ(maskBit(*reduced, 2u, 1u), std::optional<bool>(true));
    EXPECT_EQ(sampleOf<float>(*reduced, 0u, 1u, 0u), *blockMean<float>(value, finite, 2u, 0u, 1u, 0u));
    EXPECT_EQ(sampleOf<float>(*reduced, 0u, 1u, 1u), *blockMean<float>(value, finite, 2u, 0u, 1u, 1u))
        << "a block with one not-a-number of four is the mean of its three finite samples";

    // Every block of this one has a finite sample: the rule is kept and no mask carried.
    const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)> oneHole =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return !(x == 0u && y == 0u); };
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> holed =
        [oneHole](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return oneHole(x, y, c) ? 0.01f * static_cast<float>(x + 10u * y) : kNotANumber;
        };
    std::optional<an::consumer::ImageAsset> covered = finiteOnlyAsset(session, 6u, 4u, holed);
    ASSERT_TRUE(covered.has_value());
    an::Result<an::consumer::ImageAsset> reducedCovered = make(session, *covered, Made::Reduced, 2u);
    ASSERT_TRUE(reducedCovered.hasValue()) << reducedCovered.status().message();
    EXPECT_EQ(ruleOf(*reducedCovered), an::ValidityRule::FiniteOnly) << "the target's rule is kept";
    EXPECT_FALSE(maskBit(*reducedCovered, 0u, 0u).has_value()) << "no block was left, so no mask is carried";
    EXPECT_EQ(sampleOf<float>(*reducedCovered, 0u, 0u, 0u), *blockMean<float>(holed, oneHole, 2u, 0u, 0u, 0u));
}
