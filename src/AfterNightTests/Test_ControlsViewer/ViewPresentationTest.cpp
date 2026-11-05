// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The viewer's display operations over the engine: what a view paints of an image, of one of its channels and of its
/// chrominance; how a floating image with no full scale is read; how a mask is shown over what the view shows; what
/// the lens counts and reads out; and the defects these operations hold off.

#include "ControlsViewerTestSupport.h"

#include "session/DisplayProxy.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

namespace
{

using an::tests::viewer::admitted;
using an::tests::viewer::Channels;
using an::tests::viewer::makeAsset;
using an::tests::viewer::openProvider;
using an::ui::PresentedFormat;
using an::ui::PresentedImage;
using an::ui::ViewChannel;
using an::ui::ViewPresentationOutcome;
using an::ui::ViewPresentationSettings;

/// Linear presentation to bytes: what a view shows in its linear mode.
[[nodiscard]] ViewPresentationSettings linearBytes()
{
    ViewPresentationSettings settings;
    settings.request.stretch = an::PresentationStretch::Linear;
    settings.request.output = an::PresentationOutput::Interleaved8;
    return settings;
}

/// Presents an asset under a fresh admitted execution; fails the case when it is refused.
[[nodiscard]] std::optional<ViewPresentationOutcome> present(const an::consumer::EngineSession& session,
                                                             const an::consumer::ImageAsset& asset,
                                                             const ViewPresentationSettings& settings,
                                                             const an::DisplayPresentationAnalysis* reuse = nullptr)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<ViewPresentationOutcome> outcome =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, reuse, &session.memory());
    EXPECT_TRUE(outcome.hasValue()) << outcome.status().message();
    if (!outcome.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*outcome);
}

/// The byte at a pixel and channel of presented bytes.
[[nodiscard]] int byteAt(const PresentedImage& image, std::uint32_t x, std::uint32_t y, std::uint32_t channel = 0u)
{
    const std::uint8_t* pixel = image.pixel(x, y);
    return pixel != nullptr ? static_cast<int>(pixel[channel]) : -1;
}

} // namespace

TEST(ViewPresentationTest, ALinearImageIsShownAsTheNearestByteOfEachSample)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<std::uint16_t>(session, 4u, 2u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(x * 16384u + y);
        });
    ASSERT_TRUE(asset.has_value());

    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, linearBytes());
    ASSERT_TRUE(shown.has_value());
    ASSERT_NE(shown->image, nullptr);
    EXPECT_EQ(shown->image->format(), PresentedFormat::Grey8);
    EXPECT_EQ(shown->image->width(), 4u);
    EXPECT_EQ(shown->sourceWidth, 4u);
    EXPECT_EQ(byteAt(*shown->image, 0u, 0u), 0);
    EXPECT_EQ(byteAt(*shown->image, 2u, 0u), 128) << "32768 of 65535 is 127.5, rounded to even";
    EXPECT_EQ(byteAt(*shown->image, 3u, 1u), 191);
    EXPECT_FALSE(shown->domainRestated);
}

TEST(ViewPresentationTest, AnAnalysisTakenForARevisionIsReusedByItsNextPresentation)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 32u, 16u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.01f + 0.0005f * static_cast<float>(x + y);
        });
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings stretched;
    stretched.request.stretch = an::PresentationStretch::Automatic;

    const std::optional<ViewPresentationOutcome> first = present(session, *asset, stretched);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(first->analysisReused);
    EXPECT_TRUE(first->analysis.measured);

    const std::optional<ViewPresentationOutcome> second = present(session, *asset, stretched, &first->analysis);
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->analysisReused) << "the statistics of a revision are measured once";
    for (std::uint32_t x = 0u; x < 32u; ++x)
    {
        EXPECT_EQ(byteAt(*first->image, x, 5u), byteAt(*second->image, x, 5u));
    }
}

TEST(ViewPresentationTest, APreviewsAnalysisIsSetAsideByTheProductionPlanAndMeasuredAgain)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 64u, 32u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.02f + 0.0003f * static_cast<float>(x * y);
        });
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings preview;
    preview.request.stretch = an::PresentationStretch::Automatic;
    preview.subsampleFactor = 4u;

    const std::optional<ViewPresentationOutcome> fast = present(session, *asset, preview);
    ASSERT_TRUE(fast.has_value());
    EXPECT_EQ(fast->image->width(), 16u) << "the fast path presents every fourth sample of every fourth row";
    EXPECT_EQ(fast->sourceWidth, 64u);
    EXPECT_TRUE(fast->analysis.realtimePreview);

    ViewPresentationSettings production = preview;
    production.subsampleFactor = 1u;
    const std::optional<ViewPresentationOutcome> full = present(session, *asset, production, &fast->analysis);
    ASSERT_TRUE(full.has_value()) << "a preview's analysis is set aside, not an error";
    EXPECT_FALSE(full->analysisReused);
    EXPECT_FALSE(full->analysis.realtimePreview);
    EXPECT_EQ(full->image->width(), 64u);
}

TEST(ViewPresentationTest, AStoredChannelIsShownAsItsOwnPlane)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint8_t>(
        session, 3u, 2u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t channel) {
            return static_cast<std::uint8_t>(channel == 0u ? 10u : (channel == 1u ? 120u : 240u));
        });
    ASSERT_TRUE(asset.has_value());

    for (const auto& [scope, expected] : {std::pair{an::ChannelScope::Red, 10}, std::pair{an::ChannelScope::Green, 120},
                                          std::pair{an::ChannelScope::Blue, 240}})
    {
        ViewPresentationSettings settings = linearBytes();
        settings.channel.scope = scope;
        const std::optional<ViewPresentationOutcome> shown = present(session, *asset, settings);
        ASSERT_TRUE(shown.has_value());
        EXPECT_EQ(shown->image->format(), PresentedFormat::Grey8);
        EXPECT_EQ(shown->channel.scope, scope);
        EXPECT_EQ(byteAt(*shown->image, 1u, 1u), expected) << an::channelScopeName(scope);
    }

    ViewPresentationSettings whole = linearBytes();
    const std::optional<ViewPresentationOutcome> rgb = present(session, *asset, whole);
    ASSERT_TRUE(rgb.has_value());
    EXPECT_EQ(rgb->image->format(), PresentedFormat::Rgb8);
    EXPECT_EQ(byteAt(*rgb->image, 2u, 1u, 0u), 10);
    EXPECT_EQ(byteAt(*rgb->image, 2u, 1u, 2u), 240);
}

TEST(ViewPresentationTest, AComponentOfAnotherEncodingIsShownFromTheSourcesSrgbColour)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    // A grey colour image: every encoding's lightness is its grey, and its chroma is zero.
    std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
        session, 4u, 4u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(asset.has_value());

    ViewPresentationSettings lightness = linearBytes();
    lightness.channel.scope = an::ChannelScope::HsvValue;
    const std::optional<ViewPresentationOutcome> value = present(session, *asset, lightness);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->image->format(), PresentedFormat::Grey8);
    EXPECT_EQ(byteAt(*value->image, 0u, 0u), 128) << "HSV value of a 0.5 grey is 0.5";

    ViewPresentationSettings saturation = linearBytes();
    saturation.channel.scope = an::ChannelScope::HslSaturation;
    const std::optional<ViewPresentationOutcome> chroma = present(session, *asset, saturation);
    ASSERT_TRUE(chroma.has_value());
    EXPECT_EQ(byteAt(*chroma->image, 0u, 0u), 0) << "a grey has no saturation";
}

TEST(ViewPresentationTest, TheChrominanceViewShowsAStarAsRedBStarAsGreenAndNoBlue)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 4u, 4u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t channel) {
            return channel == 0u ? 0.8f : 0.2f;
        });
    ASSERT_TRUE(asset.has_value());

    ViewPresentationSettings settings = linearBytes();
    settings.channel.chrominance = true;
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, settings);
    ASSERT_TRUE(shown.has_value());
    EXPECT_TRUE(shown->channel.chrominance);
    EXPECT_EQ(shown->image->format(), PresentedFormat::Rgb8);
    EXPECT_GT(byteAt(*shown->image, 0u, 0u, 0u), 128) << "a red colour has a* above the neutral half";
    EXPECT_EQ(byteAt(*shown->image, 0u, 0u, 2u), 0) << "blue carries nothing";
}

TEST(ViewPresentationTest, AGreyImageIsShownWholeWhateverChannelIsAsked)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint8_t>(
        session, 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{60}; });
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings settings = linearBytes();
    settings.channel.scope = an::ChannelScope::CieLightness;
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, settings);
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->channel.scope, an::ChannelScope::Whole);
    EXPECT_EQ(byteAt(*shown->image, 1u, 1u), 60);
}

TEST(ViewPresentationTest, AFloatingImageWithNoFullScaleIsReadAsAFractionAndSaysSo)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
        session, 2u, 1u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x == 0u ? 0.25f : 0.75f; },
        an::NumericDomain::PhysicalScaled);
    ASSERT_TRUE(asset.has_value());
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, linearBytes());
    ASSERT_TRUE(shown.has_value());
    EXPECT_TRUE(shown->domainRestated);
    EXPECT_EQ(byteAt(*shown->image, 0u, 0u), 64);
    EXPECT_EQ(byteAt(*shown->image, 1u, 0u), 191);
}

// An unlinked automatic stretch of a four-channel image stretches its three colour channels only, and its bytes are
// wrapped as four per pixel, never as three-byte RGB, which would shear it.
TEST(ViewPresentationTest, AnImageWithAlphaIsStretchedOnItsColourAndCarriesItsAlphaUnsheared)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint16_t>(
        session, 16u, 8u, Channels::Rgba, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            if (channel == 3u)
            {
                return static_cast<std::uint16_t>(x < 8u ? 65535u : 0u);
            }
            return static_cast<std::uint16_t>(2000u + 100u * x + 37u * y + 500u * channel);
        });
    ASSERT_TRUE(asset.has_value());

    ViewPresentationSettings settings;
    settings.request.stretch = an::PresentationStretch::Automatic;
    settings.request.linked = false;
    settings.request.output = an::PresentationOutput::Rgba8;
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, settings);
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->image->format(), PresentedFormat::Rgba8);
    EXPECT_EQ(shown->analysis.colorChannels, 3u) << "alpha is not a colour channel the stretch measures";
    EXPECT_EQ(byteAt(*shown->image, 2u, 3u, 3u), 255) << "the source's own alpha is carried";
    EXPECT_EQ(byteAt(*shown->image, 12u, 3u, 3u), 0);
    EXPECT_EQ(shown->image->width(), 16u) << "four interleaved bytes per pixel, wrapped as such";
}

// The lens converts samples outside the unit interval, and non-finite ones, to defined bytes.
TEST(ViewPresentationTest, SamplesOutsideTheUnitIntervalAndNonFiniteSamplesAreShownDefined)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 4u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            switch (x)
            {
            case 0u:
                return -3.0f;
            case 1u:
                return 7.5f;
            case 2u:
                return std::numeric_limits<float>::quiet_NaN();
            default:
                return std::numeric_limits<float>::infinity();
            }
        });
    ASSERT_TRUE(asset.has_value());
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, linearBytes());
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(byteAt(*shown->image, 0u, 0u), 0);
    EXPECT_EQ(byteAt(*shown->image, 1u, 0u), 255);
    EXPECT_EQ(byteAt(*shown->image, 2u, 0u), 0) << "a non-finite sample is invalid and shown black";
    EXPECT_EQ(byteAt(*shown->image, 3u, 0u), 0);
}

TEST(ViewPresentationTest, AMaskIsShownAsATintWhoseOpacityIsWhatItProtects)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> mask =
        makeAsset<std::uint8_t>(session, 2u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x == 0u ? 255u : 0u);
        });
    ASSERT_TRUE(mask.has_value());
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mask->read();
    ASSERT_TRUE(lease.hasValue());

    an::ui::ViewMaskOverlaySettings tint;
    tint.tint = an::MaskOverlayColor{.red = 0u, .green = 255u, .blue = 0u, .alpha = 255u};
    an::Result<std::shared_ptr<const PresentedImage>> layer =
        an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, tint, nullptr, nullptr);
    ASSERT_TRUE(layer.hasValue()) << layer.status().message();
    EXPECT_EQ((*layer)->format(), PresentedFormat::Rgba8);
    EXPECT_EQ(byteAt(**layer, 0u, 0u, 3u), 0) << "a selected pixel is clear";
    EXPECT_EQ(byteAt(**layer, 1u, 0u, 1u), 255);
    EXPECT_EQ(byteAt(**layer, 1u, 0u, 3u), 255) << "a protected pixel is the tint";

    tint.invert = true;
    layer = an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, tint, nullptr, nullptr);
    ASSERT_TRUE(layer.hasValue());
    EXPECT_EQ(byteAt(**layer, 0u, 0u, 3u), 255) << "inverted, the selected pixel is protected";
    EXPECT_EQ(byteAt(**layer, 1u, 0u, 3u), 0);
}

// The multiply overlay multiplies what the view shows, not the raw samples, so a linear frame shown stretched is not
// shown nearly black.
TEST(ViewPresentationTest, AMultiplyOverlayMultipliesWhatTheViewShowsNotTheRawSamples)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> image =
        makeAsset<float>(session, 16u, 8u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.002f + 0.0002f * static_cast<float>(x + 3u * y);
        });
    std::optional<an::consumer::ImageAsset> mask =
        makeAsset<float>(session, 16u, 8u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 8u ? 1.0f : 0.5f; });
    ASSERT_TRUE(image.has_value() && mask.has_value());

    ViewPresentationSettings stretched;
    stretched.request.stretch = an::PresentationStretch::Automatic;
    const std::optional<ViewPresentationOutcome> shown = present(session, *image, stretched);
    ASSERT_TRUE(shown.has_value());

    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mask->read();
    ASSERT_TRUE(lease.hasValue());
    an::ui::ViewMaskOverlaySettings multiply;
    multiply.mode = an::MaskOverlayMode::Multiply;
    an::Result<std::shared_ptr<const PresentedImage>> layer = an::ui::renderViewMaskOverlay(
        scope->engine(), scope->execution(), *lease, multiply, shown->image.get(), nullptr);
    ASSERT_TRUE(layer.hasValue()) << layer.status().message();
    const int shownByte = byteAt(*shown->image, 4u, 4u);
    EXPECT_GT(shownByte, 50) << "the stretched frame is not black";
    EXPECT_NEAR(byteAt(**layer, 4u, 4u, 0u), shownByte, 1) << "a fully selected pixel shows what the view shows";
    EXPECT_NEAR(byteAt(**layer, 12u, 4u, 0u), byteAt(*shown->image, 12u, 4u) / 2.0, 1.0);
    EXPECT_EQ(byteAt(**layer, 12u, 4u, 3u), 255) << "a multiply is opaque";

    multiply.mode = an::MaskOverlayMode::Multiply;
    const an::Result<std::shared_ptr<const PresentedImage>> missing =
        an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, multiply, nullptr, nullptr);
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ViewPresentationTest, AColourMaskUnderAViewOfOneChannelShowsThatChannelsMask)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> mask = makeAsset<std::uint8_t>(
        session, 2u, 1u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t channel) {
            return static_cast<std::uint8_t>(channel == 1u ? 255u : 0u);
        });
    ASSERT_TRUE(mask.has_value());
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mask->read();
    ASSERT_TRUE(lease.hasValue());

    an::ui::ViewMaskOverlaySettings settings;
    settings.shownChannel.scope = an::ChannelScope::Green;
    an::Result<std::shared_ptr<const PresentedImage>> green =
        an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, settings, nullptr, nullptr);
    ASSERT_TRUE(green.hasValue()) << green.status().message();
    EXPECT_EQ(byteAt(**green, 0u, 0u, 3u), 0) << "the green channel's mask selects everything";

    settings.shownChannel.scope = an::ChannelScope::Red;
    an::Result<std::shared_ptr<const PresentedImage>> red =
        an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, settings, nullptr, nullptr);
    ASSERT_TRUE(red.hasValue());
    EXPECT_EQ(byteAt(**red, 0u, 0u, 3u), 255) << "the red channel's mask protects everything";

    settings.shownChannel.scope = an::ChannelScope::Whole;
    an::Result<std::shared_ptr<const PresentedImage>> whole =
        an::ui::renderViewMaskOverlay(scope->engine(), scope->execution(), *lease, settings, nullptr, nullptr);
    ASSERT_TRUE(whole.hasValue());
    EXPECT_EQ(byteAt(**whole, 0u, 0u, 3u), 255)
        << "the three channels stack: the opacity is one minus their product, so red and blue protect the pixel";
}

// A four-channel image passed the mask selector's colour check and was tinted as four layers, alpha among them.
TEST(ViewPresentationTest, AMaskOfFourChannelsIsRefused)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> mask = makeAsset<std::uint8_t>(
        session, 2u, 2u, Channels::Rgba, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{9}; });
    ASSERT_TRUE(mask.has_value());
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = mask->read();
    ASSERT_TRUE(lease.hasValue());
    const an::Result<std::shared_ptr<const PresentedImage>> refused = an::ui::renderViewMaskOverlay(
        scope->engine(), scope->execution(), *lease, an::ui::ViewMaskOverlaySettings{}, nullptr, nullptr);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ViewPresentationTest, ThePixelReadoutIsEachChannelAsAFractionOfFullScale)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> rgba = makeAsset<std::uint16_t>(
        session, 3u, 3u, Channels::Rgba, [](std::uint32_t x, std::uint32_t, std::uint32_t channel) {
            return static_cast<std::uint16_t>(channel == 3u ? 1u : 65535u * channel / 2u + x);
        });
    ASSERT_TRUE(rgba.has_value());
    an::Result<an::consumer::AssetReadLease> lease = rgba->read();
    ASSERT_TRUE(lease.hasValue());

    const std::optional<an::ui::PixelReadout> read = an::ui::readPixel(*lease, 0, 1);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->channels, 3u) << "alpha is not read out";
    EXPECT_DOUBLE_EQ(read->values[0], 0.0);
    EXPECT_NEAR(read->values[1], 0.5, 1e-4);
    EXPECT_DOUBLE_EQ(read->values[2], 1.0);

    const std::optional<an::ui::PixelReadout> clamped = an::ui::readPixel(*lease, 99, -5);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_NEAR(clamped->values[0], 2.0 / 65535.0, 1e-12) << "a position outside the image reads its nearest pixel";

    EXPECT_FALSE(an::ui::readPixel(an::consumer::AssetReadLease{}, 0, 0).has_value());
}

TEST(ViewPresentationTest, TheLensCountsAPresentedRegionInSixtyFourBinsOfFourCodes)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<std::uint8_t>(session, 8u, 8u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x * 4u + 3u);
        });
    ASSERT_TRUE(asset.has_value());
    const std::optional<ViewPresentationOutcome> shown = present(session, *asset, linearBytes());
    ASSERT_TRUE(shown.has_value());

    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    const an::Result<an::ui::LensHistogram> counted =
        an::ui::countPresentedRegion(scope->execution(), *shown->image, 2u, 0u, 4u, 8u);
    ASSERT_TRUE(counted.hasValue()) << counted.status().message();
    EXPECT_EQ(counted->channels, 1u);
    for (std::uint32_t bin = 0u; bin < an::ui::kLensHistogramBins; ++bin)
    {
        const std::uint64_t expected = (bin >= 2u && bin <= 5u) ? 8u : 0u;
        EXPECT_EQ(counted->counts[0][bin], expected) << "bin " << bin;
    }

    const an::ui::PixelReadout presented = an::ui::readPresentedPixel(*shown->image, 1, 0);
    EXPECT_EQ(presented.channels, 1u);
    EXPECT_NEAR(presented.values[0], 7.0 / 255.0, 1e-12);

    const an::Result<an::ui::LensHistogram> outside =
        an::ui::countPresentedRegion(scope->execution(), *shown->image, 99u, 0u, 4u, 4u);
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::EmptySelection);
}

TEST(ViewPresentationTest, ThePresentationKeyChangesWithEveryDecisionAndTheChannelKeyIsStable)
{
    ViewPresentationSettings base;
    const std::string key = an::ui::viewPresentationKey(base);
    EXPECT_EQ(key, an::ui::viewPresentationKey(base));
    ViewPresentationSettings changed = base;
    changed.request.contrast = 1.5;
    EXPECT_NE(key, an::ui::viewPresentationKey(changed));
    changed = base;
    changed.channel.scope = an::ChannelScope::Red;
    EXPECT_NE(key, an::ui::viewPresentationKey(changed));
    changed = base;
    changed.subsampleFactor = 2u;
    EXPECT_NE(key, an::ui::viewPresentationKey(changed));
    EXPECT_EQ(an::ui::viewChannelKey(ViewChannel{.chrominance = true}), "chrominance");
    EXPECT_EQ(an::ui::viewChannelKey(ViewChannel{.scope = an::ChannelScope::CieLightness}),
              std::string(an::channelScopeName(an::ChannelScope::CieLightness)));
    EXPECT_DOUBLE_EQ(an::ui::presentationBrightnessOffset(51), 0.2);
    EXPECT_DOUBLE_EQ(an::ui::presentationContrastFactor(0), 259.0 / 259.0 * 255.0 / 255.0);
}

namespace
{

/// A grey Float32 asset whose validity is a mask, each sample `value(x, y)` and each bit `valid(x, y)`.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
maskedAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
            const std::function<float(std::uint32_t, std::uint32_t)>& value,
            const std::function<bool(std::uint32_t, std::uint32_t)>& valid)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::Float32, width, height, Channels::Grey);
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{
        .validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})});
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt));
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(hold->engine(), descriptor->extent());
    EXPECT_TRUE(image.hasValue() && mask.hasValue());
    if (!image.hasValue() || !mask.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = value(x, y);
                EXPECT_TRUE(bits->setValid(0u, x, y, valid(x, y)));
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)}, std::move(*mask));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// The display proxy of an asset at a factor, reduced under a fresh admitted execution.
[[nodiscard]] std::shared_ptr<const an::ui::DisplayProxy>
proxyOf(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& asset, std::uint32_t factor)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!scope.has_value() || !lease.hasValue())
    {
        ADD_FAILURE() << "the proxy could not be reduced";
        return nullptr;
    }
    an::Result<std::shared_ptr<const an::ui::DisplayProxy>> proxy =
        an::ui::DisplayProxy::build(scope->engine(), scope->execution(), *lease, factor);
    EXPECT_TRUE(proxy.hasValue()) << proxy.status().message();
    return proxy.hasValue() ? *proxy : nullptr;
}

/// Presents an asset, or its proxy, under a fresh admitted execution; fails the case when it is refused.
[[nodiscard]] std::optional<ViewPresentationOutcome> presentPart(const an::consumer::EngineSession& session,
                                                                 const an::consumer::ImageAsset& asset,
                                                                 const ViewPresentationSettings& settings,
                                                                 const an::ui::DisplayProxy* proxy,
                                                                 const an::DisplayPresentationAnalysis* reuse = nullptr)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!scope.has_value() || !lease.hasValue())
    {
        ADD_FAILURE() << "the presentation could not be admitted";
        return std::nullopt;
    }
    an::Result<ViewPresentationOutcome> outcome =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, reuse, &session.memory(), proxy);
    EXPECT_TRUE(outcome.hasValue()) << outcome.status().message();
    if (!outcome.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*outcome);
}

/// Measures what a presentation of the whole asset presents through.
[[nodiscard]] std::optional<an::DisplayPresentationAnalysis> analysisOfWhole(const an::consumer::EngineSession& session,
                                                                             const an::consumer::ImageAsset& asset,
                                                                             const ViewPresentationSettings& settings)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!scope.has_value() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::DisplayPresentationAnalysis> analysis =
        an::ui::analyzeForView(scope->engine(), scope->execution(), *lease, settings);
    EXPECT_TRUE(analysis.hasValue()) << analysis.status().message();
    return analysis.hasValue() ? std::optional<an::DisplayPresentationAnalysis>(*analysis) : std::nullopt;
}

/// A noisy grey sky of sixteen-bit codes: a background of 2000 with a deviation near 120, independent from pixel to
/// pixel, and a star in about a thousand pixels, too few to move a block's mean often.
[[nodiscard]] std::uint16_t noisySky(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    std::uint32_t hash = x * 0x9E3779B1u + y * 0x85EBCA77u + channel * 0xC2B2AE3Du;
    hash ^= hash >> 15u;
    hash *= 0x2C1B3C6Du;
    hash ^= hash >> 12u;
    hash *= 0x297A2D39u;
    hash ^= hash >> 15u;
    const auto noise = static_cast<std::int32_t>(hash % 401u) - 200;
    const bool star = (x * 31u + y * 17u) % 1009u == 0u;
    return static_cast<std::uint16_t>(star ? 52000 : 2000 + noise);
}

} // namespace

// The quantized promotion held every sample to sixteen-bit levels, which an unsigned image of sixteen bits or
// fewer already holds: retiring it, as every view now does, changes no byte the view shows and no statistic it
// stretches by.
TEST(ViewPresentationTest, RetiringTheQuantizedPromotionChangesNoByteOrStatisticOfAnUnsignedEightOrSixteenBitImage)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> sixteen =
        makeAsset<std::uint16_t>(session, 48u, 32u, Channels::Rgb, noisySky);
    std::optional<an::consumer::ImageAsset> eight =
        makeAsset<std::uint8_t>(session, 48u, 32u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint8_t>((x * 5u + y * 3u) % 61u + 10u);
        });
    ASSERT_TRUE(sixteen.has_value() && eight.has_value());
    for (const an::consumer::ImageAsset* asset : {&*sixteen, &*eight})
    {
        for (const bool linked : {false, true})
        {
            ViewPresentationSettings gradient;
            gradient.request.stretch = an::PresentationStretch::Automatic;
            gradient.request.linked = linked;
            gradient.request.output = an::PresentationOutput::Bgra8;
            gradient.request.promotion = an::PresentationPromotion::Gradient;
            ViewPresentationSettings quantized = gradient;
            quantized.request.promotion = an::PresentationPromotion::Quantized16;
            const std::optional<ViewPresentationOutcome> full = present(session, *asset, gradient);
            const std::optional<ViewPresentationOutcome> held = present(session, *asset, quantized);
            ASSERT_TRUE(full.has_value() && held.has_value());
            ASSERT_EQ(full->image->byteCount(), held->image->byteCount());
            EXPECT_EQ(std::memcmp(full->image->data(), held->image->data(), full->image->byteCount()), 0)
                << "a byte the view shows changed";
            for (std::uint32_t channel = 0u; channel < full->analysis.colorChannels; ++channel)
            {
                EXPECT_EQ(full->analysis.medians[channel], held->analysis.medians[channel]) << channel;
                EXPECT_EQ(full->analysis.medianAbsoluteDeviations[channel],
                          held->analysis.medianAbsoluteDeviations[channel])
                    << channel;
            }
        }
    }
}

// What a view paints without converting: blue, green, red and alpha bytes, the colour premultiplied, wrapped as the
// premultiplied ARGB image Qt draws directly - the red-first bytes in the other order for an opaque pixel.
TEST(ViewPresentationTest, ABgraPresentationIsTheFormatQtPaintsWithoutConvertingIt)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint16_t>(session, 9u, 5u, Channels::Rgb, noisySky);
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings blue;
    blue.request.output = an::PresentationOutput::Bgra8;
    ViewPresentationSettings red = blue;
    red.request.output = an::PresentationOutput::Rgba8;
    const std::optional<ViewPresentationOutcome> bgra = present(session, *asset, blue);
    const std::optional<ViewPresentationOutcome> rgba = present(session, *asset, red);
    ASSERT_TRUE(bgra.has_value() && rgba.has_value());
    EXPECT_EQ(bgra->image->format(), PresentedFormat::Bgra8Premultiplied);
    EXPECT_EQ(an::ui::qtFormatOf(bgra->image->format()), QImage::Format_ARGB32_Premultiplied);
    const QImage wrapped = an::ui::presentedQImage(bgra->image);
    EXPECT_EQ(wrapped.format(), QImage::Format_ARGB32_Premultiplied);
    EXPECT_EQ(wrapped.constBits(), bgra->image->data()) << "wrapped as it is, not converted";
    for (std::uint32_t y = 0u; y < 5u; ++y)
    {
        for (std::uint32_t x = 0u; x < 9u; ++x)
        {
            EXPECT_EQ(byteAt(*bgra->image, x, y, 0u), byteAt(*rgba->image, x, y, 2u));
            EXPECT_EQ(byteAt(*bgra->image, x, y, 1u), byteAt(*rgba->image, x, y, 1u));
            EXPECT_EQ(byteAt(*bgra->image, x, y, 2u), byteAt(*rgba->image, x, y, 0u));
            EXPECT_EQ(byteAt(*bgra->image, x, y, 3u), 255);
            const QColor painted = wrapped.pixelColor(static_cast<int>(x), static_cast<int>(y));
            EXPECT_EQ(painted.red(), byteAt(*rgba->image, x, y, 0u));
            EXPECT_EQ(painted.blue(), byteAt(*rgba->image, x, y, 2u));
        }
    }
    const an::ui::PixelReadout read = an::ui::readPresentedPixel(*bgra->image, 3, 2);
    EXPECT_DOUBLE_EQ(read.values[0], byteAt(*rgba->image, 3u, 2u, 0u) / 255.0) << "the readout is red first";
}

// Averaging a block narrows the noise a median absolute deviation measures, so a proxy's own statistics would stretch
// it harder than the image it stands for. A proxy is presented through the whole image's statistics - measured here
// when none is handed - never through its own.
TEST(ViewPresentationTest, AProxyIsStretchedByTheWholeImagesStatisticsNeverItsOwn)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<std::uint16_t>(session, 140u, 98u, Channels::Grey, noisySky);
    ASSERT_TRUE(asset.has_value());
    const std::shared_ptr<const an::ui::DisplayProxy> proxy = proxyOf(session, *asset, 7u);
    ASSERT_NE(proxy, nullptr);

    ViewPresentationSettings stretched;
    stretched.request.stretch = an::PresentationStretch::Automatic;
    stretched.request.output = an::PresentationOutput::Bgra8;
    stretched.proxyFactor = 7u;
    const std::optional<ViewPresentationOutcome> shown = presentPart(session, *asset, stretched, proxy.get());
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->image->width(), 20u);
    EXPECT_EQ(shown->image->height(), 14u);
    EXPECT_EQ(shown->factor, 7u);
    EXPECT_EQ(shown->covered, (an::ui::ViewRegion{.x = 0u, .y = 0u, .width = 140u, .height = 98u}));
    EXPECT_EQ(shown->sourceWidth, 140u);
    EXPECT_TRUE(shown->measuredWhole);
    EXPECT_GT(shown->wholeMeasureTime, std::chrono::nanoseconds::zero());
    EXPECT_FALSE(shown->analysisReused);

    const std::optional<an::DisplayPresentationAnalysis> whole = analysisOfWhole(session, *asset, stretched);
    ASSERT_TRUE(whole.has_value());
    EXPECT_EQ(shown->analysis.measuredSamples[0], 140u * 98u) << "every sample of the image was measured";
    EXPECT_EQ(shown->analysis.medians[0], whole->medians[0]);
    EXPECT_EQ(shown->analysis.medianAbsoluteDeviations[0], whole->medianAbsoluteDeviations[0]);

    // The proxy's own deviation is the narrower one the stretch must not take.
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        ASSERT_TRUE(scope.has_value());
        const an::Result<an::DisplayPresentationPlan> plan = an::planDisplayPresentation(stretched.request);
        ASSERT_TRUE(plan.hasValue());
        const an::KernelValidityView validity = proxy->validity();
        const an::Result<an::DisplayPresentationAnalysis> proxied =
            an::analyzeDisplayPresentation(scope->execution(), *plan,
                                           an::DisplayPresentationSource{.descriptor = &proxy->descriptor(),
                                                                         .samples = proxy->samples(),
                                                                         .layout = &proxy->layout(),
                                                                         .validity = validity});
        ASSERT_TRUE(proxied.hasValue()) << proxied.status().message();
        EXPECT_LT(proxied->medianAbsoluteDeviations[0], 0.6 * whole->medianAbsoluteDeviations[0])
            << "the proxy's averaging narrows its deviation, which is why it is never measured";
    }

    // A handed analysis of the revision is presented through as it is.
    const std::optional<ViewPresentationOutcome> again = presentPart(session, *asset, stretched, proxy.get(), &*whole);
    ASSERT_TRUE(again.has_value());
    EXPECT_TRUE(again->analysisReused);
    EXPECT_FALSE(again->measuredWhole);
    EXPECT_EQ(again->wholeMeasureTime, std::chrono::nanoseconds::zero());
    EXPECT_EQ(std::memcmp(again->image->data(), shown->image->data(), shown->image->byteCount()), 0);
}

TEST(ViewPresentationTest, ARegionCoversWhatItAskedForAndIsStretchedAsTheWholeImageIs)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<std::uint16_t>(session, 64u, 40u, Channels::Rgb, noisySky);
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings stretched;
    stretched.request.stretch = an::PresentationStretch::Automatic;
    stretched.request.output = an::PresentationOutput::Bgra8;
    const std::optional<ViewPresentationOutcome> whole = present(session, *asset, stretched);
    ASSERT_TRUE(whole.has_value());

    ViewPresentationSettings part = stretched;
    part.region = an::ui::ViewRegion{.x = 10u, .y = 6u, .width = 20u, .height = 12u};
    const std::optional<ViewPresentationOutcome> region = presentPart(session, *asset, part, nullptr);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->covered, *part.region);
    EXPECT_EQ(region->image->width(), 20u);
    EXPECT_TRUE(region->measuredWhole);
    EXPECT_EQ(region->analysis.medians[1], whole->analysis.medians[1]) << "the region is stretched as the image is";
    for (std::uint32_t y = 0u; y < 12u; ++y)
    {
        for (std::uint32_t x = 0u; x < 20u; ++x)
        {
            for (std::uint32_t byte = 0u; byte < 4u; ++byte)
            {
                ASSERT_EQ(byteAt(*region->image, x, y, byte), byteAt(*whole->image, x + 10u, y + 6u, byte))
                    << x << " " << y << " " << byte;
            }
        }
    }

    // A region of a proxy is presented from the proxy's pixels that cover it.
    const std::shared_ptr<const an::ui::DisplayProxy> proxy = proxyOf(session, *asset, 4u);
    ASSERT_NE(proxy, nullptr);
    part.proxyFactor = 4u;
    part.region = an::ui::ViewRegion{.x = 9u, .y = 5u, .width = 22u, .height = 14u};
    const std::optional<ViewPresentationOutcome> reduced = presentPart(session, *asset, part, proxy.get());
    ASSERT_TRUE(reduced.has_value());
    EXPECT_EQ(reduced->covered, (an::ui::ViewRegion{.x = 8u, .y = 4u, .width = 24u, .height = 16u}))
        << "rounded outward to whole blocks";
    EXPECT_EQ(reduced->image->width(), 6u);
    EXPECT_EQ(reduced->factor, 4u);

    part.proxyFactor = 3u;
    const an::Result<ViewPresentationOutcome> mismatched = [&]() {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        return an::ui::presentForView(scope->engine(), scope->execution(), *lease, part, nullptr, nullptr, proxy.get());
    }();
    ASSERT_FALSE(mismatched.hasValue()) << "a proxy of another factor is refused";
    EXPECT_EQ(mismatched.status().code(), an::ErrorCode::InvalidArgument);
}

// A mask has no narrower view, so a region of an image whose validity is a mask was refused; it is cut, with its mask,
// and shows what the whole presentation shows there.
TEST(ViewPresentationTest, ARegionOfAnImageWhoseValidityIsAMaskIsCutWithItsMask)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset = maskedAsset(
        session, 30u, 20u, [](std::uint32_t x, std::uint32_t y) { return 0.2f + 0.01f * static_cast<float>(x + y); },
        [](std::uint32_t x, std::uint32_t y) { return (x + 2u * y) % 7u != 0u; });
    ASSERT_TRUE(asset.has_value());
    ViewPresentationSettings settings = linearBytes();
    const std::optional<ViewPresentationOutcome> whole = present(session, *asset, settings);
    ASSERT_TRUE(whole.has_value());
    settings.region = an::ui::ViewRegion{.x = 5u, .y = 3u, .width = 11u, .height = 9u};
    const std::optional<ViewPresentationOutcome> region = presentPart(session, *asset, settings, nullptr);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->image->width(), 11u);
    for (std::uint32_t y = 0u; y < 9u; ++y)
    {
        for (std::uint32_t x = 0u; x < 11u; ++x)
        {
            EXPECT_EQ(byteAt(*region->image, x, y), byteAt(*whole->image, x + 5u, y + 3u)) << x << " " << y;
        }
    }
    EXPECT_EQ(byteAt(*region->image, 3u, 0u), 0) << "a sample the mask rejects is shown black, as in the whole image";
    EXPECT_GT(byteAt(*region->image, 2u, 2u), 0);
}

// A mask shown over a zoomed-out view is rendered from the mask's proxy at the image's factor and over its region, so
// the layer has the presented image's size, a multiply included.
TEST(ViewPresentationTest, AMaskLayerIsRenderedAtTheImagesReductionAndOverItsRegion)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> image =
        makeAsset<std::uint16_t>(session, 60u, 40u, Channels::Grey, noisySky);
    std::optional<an::consumer::ImageAsset> mask =
        makeAsset<float>(session, 60u, 40u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 30u ? 1.0f : 0.25f; });
    ASSERT_TRUE(image.has_value() && mask.has_value());
    const std::shared_ptr<const an::ui::DisplayProxy> imageProxy = proxyOf(session, *image, 4u);
    const std::shared_ptr<const an::ui::DisplayProxy> maskProxy = proxyOf(session, *mask, 4u);
    ASSERT_NE(imageProxy, nullptr);
    ASSERT_NE(maskProxy, nullptr);

    ViewPresentationSettings settings;
    settings.request.stretch = an::PresentationStretch::Automatic;
    settings.request.output = an::PresentationOutput::Interleaved8;
    settings.proxyFactor = 4u;
    settings.region = an::ui::ViewRegion{.x = 8u, .y = 4u, .width = 40u, .height = 24u};
    const std::optional<ViewPresentationOutcome> shown = presentPart(session, *image, settings, imageProxy.get());
    ASSERT_TRUE(shown.has_value());

    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = mask->read();
    ASSERT_TRUE(scope.has_value() && lease.hasValue());
    for (const an::MaskOverlayMode mode : {an::MaskOverlayMode::Tint, an::MaskOverlayMode::Multiply})
    {
        an::ui::ViewMaskOverlaySettings overlay;
        overlay.mode = mode;
        overlay.proxyFactor = 4u;
        overlay.region = settings.region;
        const an::Result<std::shared_ptr<const PresentedImage>> layer = an::ui::renderViewMaskOverlay(
            scope->engine(), scope->execution(), *lease, overlay,
            mode == an::MaskOverlayMode::Multiply ? shown->image.get() : nullptr, nullptr, maskProxy.get());
        ASSERT_TRUE(layer.hasValue()) << layer.status().message();
        EXPECT_EQ((*layer)->width(), shown->image->width());
        EXPECT_EQ((*layer)->height(), shown->image->height());
        if (mode == an::MaskOverlayMode::Multiply)
        {
            EXPECT_NEAR(byteAt(**layer, 1u, 1u, 0u), byteAt(*shown->image, 1u, 1u), 1)
                << "a mask of one shows the image";
            EXPECT_NEAR(byteAt(**layer, 9u, 1u, 0u), byteAt(*shown->image, 9u, 1u) / 4.0, 1.0);
        }
    }
}

// A presentation of a part that may not measure - the lens's, on the GUI thread - returns the refusal of an analysis
// the engine sets aside rather than measuring the whole image there, and never measures the part on itself; one that
// may measure measures the whole image.
TEST(ViewPresentationTest, APartThatMayNotMeasureReturnsTheRefusalOfAnAnalysisSetAside)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 64u, 32u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.02f + 0.0003f * static_cast<float>(x * y);
        });
    ASSERT_TRUE(asset.has_value());

    // A preview's analysis, which the production plan refuses.
    ViewPresentationSettings preview;
    preview.request.stretch = an::PresentationStretch::Automatic;
    preview.subsampleFactor = 4u;
    const std::optional<ViewPresentationOutcome> fast = present(session, *asset, preview);
    ASSERT_TRUE(fast.has_value());
    ASSERT_TRUE(fast->analysis.realtimePreview);

    ViewPresentationSettings region;
    region.request.stretch = an::PresentationStretch::Automatic;
    region.request.output = an::PresentationOutput::Bgra8;
    region.region = an::ui::ViewRegion{.x = 8u, .y = 4u, .width = 32u, .height = 16u};

    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());

    const an::Result<ViewPresentationOutcome> refused =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, region, &fast->analysis, &session.memory(),
                               nullptr, an::ui::WholeMeasurement::Refused);
    ASSERT_FALSE(refused.hasValue()) << "a part that may not measure presented through a refused analysis";
    EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);

    const an::Result<ViewPresentationOutcome> measured =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, region, &fast->analysis, &session.memory(),
                               nullptr, an::ui::WholeMeasurement::Measured);
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_TRUE(measured->measuredWhole);
    EXPECT_FALSE(measured->analysis.realtimePreview);
    EXPECT_EQ(measured->image->width(), 32u);
}
