// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stretch of an unsigned eight- or sixteen-bit image as one lookup of its codes: what a stretch dialog's body
/// publishes through `publishStretchOfSource` is what the working copy publishes - every sample, the description, the
/// metadata and the validity - for every code wherever it lies, in the last partial vector of a row as in its middle,
/// with one transfer for every channel, a transfer of each channel's own and a channel left alone; and an image or a
/// stretch a lookup could not answer exactly (`stretchLooksUpCodes`) is left to the working copy.
#include "StretchingTestSupport.h"

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

namespace an::tests::processes::stretching
{
namespace
{

using an::tests::viewer::Channels;

/// An odd width leaves a partial vector at the end of every row, whatever the lanes; with the height, more pixels than
/// a sixteen-bit image has codes.
constexpr std::uint32_t kWidth = 263u;
constexpr std::uint32_t kHeight = 257u;

/// A stretch a case runs, named for the report.
struct NamedStretch
{
    const char* name = "";
    an::StretchImageRequest request{};
    Channels channels = Channels::Rgb;
};

/// A screen transfer of three channels, each by its own parameters.
[[nodiscard]] an::StretchImageRequest screenTransfer(const std::array<an::MidtonesTransferParameters, 3u>& channels)
{
    an::StretchImageRequest request;
    request.stretch.function = an::StretchFunction::ScreenTransfer;
    request.stretch.screenTransfer = channels;
    request.colorMode = an::StretchColorMode::Independent;
    return request;
}

constexpr an::MidtonesTransferParameters kCommon{.midtones = 0.12, .shadows = 0.02, .highlights = 0.95};

/// The stretches a lookup answers exactly: every channel by one transfer, as the histogram transformation's combined
/// channel stretches it; each channel by its own; one channel left alone; a generalized hyperbolic and a linear
/// stretch of every channel; and an asinh curve over a grey image.
[[nodiscard]] std::vector<NamedStretch> stretchesLookedUp()
{
    std::vector<NamedStretch> stretches;
    stretches.push_back(
        {.name = "one transfer for every channel", .request = screenTransfer({kCommon, kCommon, kCommon})});
    stretches.push_back(
        {.name = "a transfer of each channel's own",
         .request = screenTransfer(
             {kCommon, an::MidtonesTransferParameters{.midtones = 0.3, .shadows = 0.0, .highlights = 1.0},
              an::MidtonesTransferParameters{.midtones = 0.6, .shadows = 0.05, .highlights = 0.8}})});
    an::StretchImageRequest leftAlone = screenTransfer({kCommon, kCommon, kCommon});
    leftAlone.channelEnabled = {true, false, true, false};
    stretches.push_back({.name = "the green channel left alone", .request = leftAlone});
    an::StretchImageRequest hyperbolic;
    hyperbolic.stretch.function = an::StretchFunction::GeneralizedHyperbolic;
    hyperbolic.stretch.amount = 3.0;
    hyperbolic.stretch.bias = 2.0;
    hyperbolic.stretch.symmetryPoint = 0.1;
    stretches.push_back({.name = "a generalized hyperbolic curve", .request = hyperbolic});
    an::StretchImageRequest linear;
    linear.stretch.function = an::StretchFunction::Linear;
    linear.stretch.blackPoint = 0.05;
    linear.stretch.whitePoint = 0.9;
    stretches.push_back({.name = "a linear stretch", .request = linear});
    an::StretchImageRequest asinh;
    asinh.stretch.function = an::StretchFunction::Asinh;
    asinh.stretch.amount = 20.0;
    stretches.push_back({.name = "an asinh curve over a grey image", .request = asinh, .channels = Channels::Grey});
    return stretches;
}

/// Every code of the representation in every channel, each channel's codes in an order of their own: a multiplier
/// prime to the number of codes walks through all of them, from a start that differs by channel.
template <typename SampleT> [[nodiscard]] SampleT everyCodeAt(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
    const std::uint64_t index = static_cast<std::uint64_t>(y) * kWidth + x;
    return static_cast<SampleT>((index * 7919u + static_cast<std::uint64_t>(channel) * 104729u) % kCodes);
}

/// Publishes a stretch of an image of every code both ways - through `publishStretchOfSource` and through the working
/// copy - and expects one image of them.
template <typename SampleT>
void expectPublishedAsTheWorkingCopyPublishes(const an::consumer::EngineSession& session, const NamedStretch& stretch)
{
    SCOPED_TRACE(std::string(stretch.name) + ", " + std::to_string(8u * sizeof(SampleT)) + " bits");
    std::optional<an::consumer::ImageAsset> asset =
        an::tests::viewer::makeAsset<SampleT>(session, kWidth, kHeight, stretch.channels, everyCodeAt<SampleT>);
    ASSERT_TRUE(asset.has_value());
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    ASSERT_TRUE(lease.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    const an::KernelValidityView validity =
        an::ui::processes::stretchValidity(source.metadata(), source.validity(), maskView);
    EXPECT_TRUE(an::ui::processes::stretchLooksUpCodes(source.image(), stretch.request, validity))
        << "the stretch was not looked up";
    const an::ui::processes::StretchRecord record{.effect = an::kStretchImageEffect,
                                                  .operation = "an::stretchImage",
                                                  .detail = stretch.name,
                                                  .processingOperation = "Code lookup case"};

    an::Result<an::consumer::AnyImage> working =
        an::ui::processes::makeStretchWorkingCopy(scope->engine(), scope->execution(), source.image());
    ASSERT_TRUE(working.hasValue()) << working.status().message();
    const an::Status stretched = an::ui::processes::stretchWorkingImage(scope->engine(), scope->execution(), *working,
                                                                        stretch.request, validity);
    ASSERT_TRUE(stretched.ok()) << stretched.message();
    an::Result<an::ui::ProcessDestination> throughCopy =
        an::ui::ProcessDestination::unwrittenFor(session, scope->engine(), source);
    ASSERT_TRUE(throughCopy.hasValue()) << throughCopy.status().message();
    const an::Status copied = an::ui::processes::publishStretchResult(session, scope->engine(), scope->execution(),
                                                                      source, *working, QRect(), record, *throughCopy);
    ASSERT_TRUE(copied.ok()) << copied.message();

    an::Result<an::ui::ProcessDestination> throughLookup =
        an::ui::ProcessDestination::unwrittenFor(session, scope->engine(), source);
    ASSERT_TRUE(throughLookup.hasValue()) << throughLookup.status().message();
    const an::Status looked = an::ui::processes::publishStretchOfSource(
        session, scope->engine(), scope->execution(), source, stretch.request, validity, record, *throughLookup);
    ASSERT_TRUE(looked.ok()) << looked.message();

    an::Result<an::consumer::ImageAsset> expected = throughCopy->release();
    an::Result<an::consumer::ImageAsset> actual = throughLookup->release();
    ASSERT_TRUE(expected.hasValue() && actual.hasValue());
    EXPECT_EQ(actual->descriptor(), expected->descriptor());
    {
        an::Result<an::consumer::AssetReadLease> left = actual->read();
        an::Result<an::consumer::AssetReadLease> right = expected->read();
        ASSERT_TRUE(left.hasValue() && right.hasValue());
        EXPECT_TRUE(an::ui::ProcessSource::of(*left).metadata() == an::ui::ProcessSource::of(*right).metadata());
        EXPECT_EQ(left->validity() == nullptr, right->validity() == nullptr);
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> got = fixtures::engine::readAssetSamples(*actual, error);
    ASSERT_TRUE(got.has_value()) << error;
    const std::optional<fixtures::FixtureSamples> want = fixtures::engine::readAssetSamples(*expected, error);
    ASSERT_TRUE(want.has_value()) << error;
    EXPECT_EQ(differingSamplesIn(*got, *want, QRect(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight))), 0u);
}

/// An image of the engine's own, of a representation, size, channels and arrangement; empty when it cannot be made.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::AnyImage> imageOf(const an::EngineContext& engine, std::uint32_t width,
                                                            std::uint32_t height, Channels channels,
                                                            an::StorageArrangement arrangement)
{
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(engine, an::kSampleTypeOf<SampleT>, width, height, channels);
    if (!described.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::create(engine, *described, arrangement, an::StorageInitialization::Zeroed);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    return an::consumer::AnyImage(std::move(*image));
}

} // namespace

TEST(StretchCodeLookupTest, ASixteenBitImageIsPublishedAsTheWorkingCopyPublishesIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    for (const NamedStretch& stretch : stretchesLookedUp())
    {
        expectPublishedAsTheWorkingCopyPublishes<std::uint16_t>(context->engineSession(), stretch);
    }
}

TEST(StretchCodeLookupTest, AnEightBitImageIsPublishedAsTheWorkingCopyPublishesIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    for (const NamedStretch& stretch : stretchesLookedUp())
    {
        expectPublishedAsTheWorkingCopyPublishes<std::uint8_t>(context->engineSession(), stretch);
    }
}

// A lookup answers a code the same wherever it lies only for a stretch of each channel alone, of a planar unsigned
// eight- or sixteen-bit image every sample of which is valid; it is worth its table only for an image of at least as
// many pixels as codes. Anything else is left to the working copy.
TEST(StretchCodeLookupTest, WhatALookupCouldNotAnswerExactlyIsLeftToTheWorkingCopy)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    const an::EngineContext& engine = scope->engine();
    const an::StretchImageRequest transfer = screenTransfer({kCommon, kCommon, kCommon});
    const an::KernelValidityView everyValid{};
    const auto looksUp = [&](const std::optional<an::consumer::AnyImage>& image, const an::StretchImageRequest& request,
                             const an::KernelValidityView& validity) {
        EXPECT_TRUE(image.has_value());
        return image.has_value() && an::ui::processes::stretchLooksUpCodes(*image, request, validity);
    };
    const auto planar = an::StorageArrangement::Planar;

    EXPECT_TRUE(looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer, everyValid));
    EXPECT_TRUE(looksUp(imageOf<std::uint16_t>(engine, 256u, 256u, Channels::Rgb, planar), transfer, everyValid))
        << "an image of as many pixels as codes";
    EXPECT_FALSE(looksUp(imageOf<std::uint16_t>(engine, 255u, 256u, Channels::Rgb, planar), transfer, everyValid))
        << "an image of fewer pixels than codes";
    EXPECT_TRUE(looksUp(imageOf<std::uint8_t>(engine, 16u, 16u, Channels::Rgb, planar), transfer, everyValid));
    EXPECT_FALSE(looksUp(imageOf<std::uint8_t>(engine, 15u, 16u, Channels::Rgb, planar), transfer, everyValid));

    EXPECT_FALSE(looksUp(imageOf<float>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer, everyValid));
    EXPECT_FALSE(looksUp(imageOf<std::int16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer, everyValid));
    EXPECT_FALSE(looksUp(imageOf<std::uint32_t>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer, everyValid));
    EXPECT_FALSE(
        looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, an::StorageArrangement::Interleaved),
                transfer, everyValid))
        << "an interleaved image";

    const std::optional<an::ValidityDescriptor> finite =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    const std::optional<an::ValidityDescriptor> zeroes =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    ASSERT_TRUE(finite.has_value() && zeroes.has_value());
    EXPECT_TRUE(looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer,
                        an::KernelValidityView{.descriptor = *finite}))
        << "every integer sample is finite";
    EXPECT_FALSE(looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), transfer,
                         an::KernelValidityView{.descriptor = *zeroes}))
        << "a rule that decides pixel by pixel";

    an::StretchImageRequest luminance = transfer;
    luminance.colorMode = an::StretchColorMode::HumanLuminance;
    EXPECT_FALSE(looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), luminance, everyValid))
        << "a stretch that couples the colour channels";
    EXPECT_TRUE(looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Grey, planar), luminance, everyValid))
        << "a grey image has no channels to couple";
    an::StretchImageRequest linearLuminance = luminance;
    linearLuminance.stretch.function = an::StretchFunction::Linear;
    EXPECT_TRUE(
        looksUp(imageOf<std::uint16_t>(engine, kWidth, kHeight, Channels::Rgb, planar), linearLuminance, everyValid))
        << "a linear stretch is independent whatever its mode";
}

} // namespace an::tests::processes::stretching
