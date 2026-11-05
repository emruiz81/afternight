// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The engine work the framework does around a process body: the destination that starts as a copy of the source, the
/// channel scope a run snapshotted and the metadata it keeps, the modulation and the mask blended back toward the
/// source, the conversions of what a run reads that the next run reads again and the dialog keeps between runs, the
/// preview area's crop, and the composite of a processed patch over the whole image.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/ProcessExecution.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;

/// A session for the cases that call the helpers directly.
struct Session
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openFileProvider(2u);

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return provider->engineSession();
    }
};

/// A colour Float32 image whose samples are `value(x, y, channel)`, carrying one FITS card, as an asset.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
colourAssetWithCard(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
                    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::Float32, width, height, Channels::Rgb);
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M42'", "target");
    an::Result<an::CodecMetadata> codec = card.hasValue() ? an::CodecMetadata::create(an::SourceEncoding{}, {*card})
                                                          : an::Result<an::CodecMetadata>(card.status());
    if (!described.has_value() || !codec.hasValue())
    {
        return std::nullopt;
    }
    an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, *codec, std::nullopt);
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// The description an image holds, or nothing.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOfImage(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// The keywords of an asset's codec cards.
[[nodiscard]] std::set<std::string> cardKeywordsOf(const an::consumer::ImageAsset& asset)
{
    std::set<std::string> keywords;
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return keywords;
    }
    std::visit(
        [&](const auto& typed) {
            for (const an::CodecCard& card : typed.metadata().codec().cards())
            {
                keywords.insert(std::string(card.keyword()));
            }
        },
        lease->image());
    return keywords;
}

/// An operation that writes a value into every sample of its plane.
class FillOperation final : public an::ChannelScopeOperation
{
public:
    explicit FillOperation(double value) : m_value(value) {}

    [[nodiscard]] an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                                 const an::ChannelScopeOutput& output) override
    {
        ++calls;
        planeChannels = input.descriptor->extent().channelCount();
        return scalePlane(input, output, 0.0).ok() ? addTo(output) : an::Status::failure(an::ErrorCode::Internal, "");
    }

    int calls = 0;
    std::uint32_t planeChannels = 0u;

private:
    [[nodiscard]] an::Status addTo(const an::ChannelScopeOutput& output) const
    {
        auto* samples = static_cast<float*>(output.samples);
        const an::ImageLayout& layout = *output.layout;
        if (output.descriptor->sampleType() != an::SampleType::Float32)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "a Float32 plane is expected");
        }
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            auto* row = reinterpret_cast<float*>(reinterpret_cast<std::byte*>(samples) + *layout.rowByteOffset(0u, y));
            for (std::uint32_t x = 0u; x < layout.extent().width(); ++x)
            {
                row[x] = static_cast<float>(m_value);
            }
        }
        return an::Status::success();
    }

    double m_value;
};

} // namespace

TEST(ProcessDestinationTest, StartsAsACopyOfTheSourceItsDescriptionAndItsCards)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source =
        colourAssetWithCard(s.session(), 6u, 4u, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(x + 10u * y + 100u * c) / 1000.0f;
        });
    ASSERT_TRUE(source.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());

    an::Result<an::ui::ProcessDestination> destination = an::ui::ProcessDestination::copyOf(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*lease));
    ASSERT_TRUE(destination.hasValue()) << destination.status().message();
    an::Result<an::consumer::ImageAsset> released = destination->release();
    ASSERT_TRUE(released.hasValue());

    EXPECT_EQ(released->descriptor(), source->descriptor());
    EXPECT_NE(released->revision().assetIdentity, source->revision().assetIdentity);
    EXPECT_EQ(cardKeywordsOf(*released), (std::set<std::string>{"OBJECT"}));
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_FLOAT_EQ(assetSample(*released, 5u, 3u, c), static_cast<float>(5u + 30u + 100u * c) / 1000.0f);
    }
}

TEST(ProcessDestinationTest, IsPlanarWhateverTheSourcesArrangement)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::UInt16, 3u, 2u, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<std::uint16_t>> interleaved = an::Image<std::uint16_t>::create(
        scope->engine(), *described, an::StorageArrangement::Interleaved, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(interleaved.hasValue());
    {
        // One plane holds every channel: the blue sample of pixel (1, 1) is that pixel's third.
        std::optional<an::ImageWriteAccess<std::uint16_t>> access = interleaved->write();
        ASSERT_EQ(access->view().layout().samplesPerPixel(), 3u);
        *access->view().sampleAt(0u, 1u, 1u, 2u) = 4242u;
    }
    an::Result<an::consumer::ImageAsset> source =
        an::consumer::ImageAsset::adopt(s.session(), an::consumer::AnyImage(std::move(*interleaved)));
    ASSERT_TRUE(source.hasValue());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());

    an::Result<an::ui::ProcessDestination> destination = an::ui::ProcessDestination::copyOf(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*lease));
    ASSERT_TRUE(destination.hasValue()) << destination.status().message();
    const auto* typed = std::get_if<an::Image<std::uint16_t>>(&destination->asset().image());
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->view()->layout().samplesPerPixel(), 1u);
    EXPECT_EQ(*typed->view()->sampleAt(2u, 1u, 1u, 0u), 4242u);
}

TEST(ProcessDestinationTest, ReplaceMakesAnotherImageTheDestinationAndReleaseHandsItOver)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::ui::ImageDocument> document =
        greyDocument(s.session(), 4u, 4u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = document->read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::ui::ProcessDestination> destination = an::ui::ProcessDestination::copyOf(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*lease));
    ASSERT_TRUE(destination.hasValue());

    std::optional<an::consumer::ImageAsset> other = makeAsset<float>(
        s.session(), 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.75f; });
    ASSERT_TRUE(other.has_value());
    const std::uint64_t otherIdentity = other->revision().assetIdentity;
    ASSERT_TRUE(destination->replace(std::move(*other)).ok());
    an::Result<an::consumer::ImageAsset> released = destination->release();
    ASSERT_TRUE(released.hasValue());
    EXPECT_EQ(released->revision().assetIdentity, otherIdentity);
    EXPECT_FLOAT_EQ(assetSample(*released, 1u, 1u), 0.75f);
    EXPECT_FALSE(destination->valid());
    EXPECT_FALSE(destination->release().hasValue());
}

// A body that writes every sample is handed a destination with the source's description, cards and validity whose
// samples start unwritten - when the source is planar and every sample of it valid, which is what lets every helper
// read it where it lies. Otherwise, and for a body that writes part, the destination is a copy; and a destination
// that starts unwritten holds the source once asked to.
TEST(ProcessDestinationTest, ABodyThatWritesEverySampleOverAPlanarImageEverySampleOfWhichIsValidIsHandedNoCopy)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source =
        colourAssetWithCard(s.session(), 6u, 4u, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(x + 10u * y + 100u * c) / 1000.0f;
        });
    ASSERT_TRUE(source.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);

    an::Result<an::ui::ProcessDestination> part = an::ui::ProcessDestination::forBody(
        s.session(), scope->engine(), scope->execution(), processSource, an::ui::ProcessBodyCoverage::WritesPart);
    ASSERT_TRUE(part.hasValue()) << part.status().message();
    EXPECT_TRUE(part->holdsSource());

    an::Result<an::ui::ProcessDestination> every =
        an::ui::ProcessDestination::forBody(s.session(), scope->engine(), scope->execution(), processSource,
                                            an::ui::ProcessBodyCoverage::WritesEverySample);
    ASSERT_TRUE(every.hasValue()) << every.status().message();
    EXPECT_FALSE(every->holdsSource());
    EXPECT_EQ(descriptorOfImage(every->asset().image()), source->descriptor());
    // The source's samples arrive once the destination is asked to hold them, and its cards were there from the start.
    ASSERT_TRUE(every->holdSource(scope->execution(), processSource).ok());
    EXPECT_TRUE(every->holdsSource());
    an::Result<an::consumer::ImageAsset> held = every->release();
    ASSERT_TRUE(held.hasValue());
    EXPECT_NE(held->revision().assetIdentity, source->revision().assetIdentity);
    EXPECT_EQ(cardKeywordsOf(*held), (std::set<std::string>{"OBJECT"}));
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_FLOAT_EQ(assetSample(*held, 5u, 3u, c), static_cast<float>(5u + 30u + 100u * c) / 1000.0f);
    }

    // An interleaved source is copied, planar, whatever the body declares.
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::UInt16, 3u, 2u, Channels::Rgb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<std::uint16_t>> interleaved = an::Image<std::uint16_t>::create(
        scope->engine(), *described, an::StorageArrangement::Interleaved, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(interleaved.hasValue());
    an::Result<an::consumer::ImageAsset> interleavedAsset =
        an::consumer::ImageAsset::adopt(s.session(), an::consumer::AnyImage(std::move(*interleaved)));
    ASSERT_TRUE(interleavedAsset.hasValue());
    an::Result<an::consumer::AssetReadLease> interleavedLease = interleavedAsset->read();
    ASSERT_TRUE(interleavedLease.hasValue());
    an::Result<an::ui::ProcessDestination> copied = an::ui::ProcessDestination::forBody(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*interleavedLease),
        an::ui::ProcessBodyCoverage::WritesEverySample);
    ASSERT_TRUE(copied.hasValue()) << copied.status().message();
    EXPECT_TRUE(copied->holdsSource());

    // So is a source with a mask, which a body honours by leaving the samples it rejects alone.
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(scope->engine(), described->extent());
    ASSERT_TRUE(mask.hasValue());
    an::Result<an::Image<std::uint16_t>> planar = an::Image<std::uint16_t>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(planar.hasValue());
    an::Result<an::consumer::ImageAsset> masked =
        an::consumer::ImageAsset::adopt(s.session(), an::consumer::AnyImage(std::move(*planar)), std::move(*mask));
    ASSERT_TRUE(masked.hasValue()) << masked.status().message();
    an::Result<an::consumer::AssetReadLease> maskedLease = masked->read();
    ASSERT_TRUE(maskedLease.hasValue());
    an::Result<an::ui::ProcessDestination> maskedDestination = an::ui::ProcessDestination::forBody(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*maskedLease),
        an::ui::ProcessBodyCoverage::WritesEverySample);
    ASSERT_TRUE(maskedDestination.hasValue()) << maskedDestination.status().message();
    EXPECT_TRUE(maskedDestination->holdsSource());

    // And so is a source under a value rule, whose samples the rule rejects a body leaves alone as it leaves a mask's.
    const an::Result<an::ImageSemantics> finiteOnly = an::ImageSemantics::create(
        an::ImageSemanticsRequest{.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})});
    ASSERT_TRUE(finiteOnly.hasValue());
    const std::optional<an::ImageDescriptor> floating =
        an::tests::viewer::describeImage(scope->engine(), an::SampleType::Float32, 3u, 2u, Channels::Rgb);
    ASSERT_TRUE(floating.has_value());
    an::Result<an::Image<float>> ruled = an::Image<float>::create(
        scope->engine(), *floating, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *finiteOnly, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt));
    ASSERT_TRUE(ruled.hasValue()) << ruled.status().message();
    an::Result<an::consumer::ImageAsset> ruledAsset =
        an::consumer::ImageAsset::adopt(s.session(), an::consumer::AnyImage(std::move(*ruled)));
    ASSERT_TRUE(ruledAsset.hasValue()) << ruledAsset.status().message();
    an::Result<an::consumer::AssetReadLease> ruledLease = ruledAsset->read();
    ASSERT_TRUE(ruledLease.hasValue());
    an::Result<an::ui::ProcessDestination> ruledDestination = an::ui::ProcessDestination::forBody(
        s.session(), scope->engine(), scope->execution(), an::ui::ProcessSource::of(*ruledLease),
        an::ui::ProcessBodyCoverage::WritesEverySample);
    ASSERT_TRUE(ruledDestination.hasValue()) << ruledDestination.status().message();
    EXPECT_TRUE(ruledDestination->holdsSource()) << "a source under a value rule starts the destination as a copy";
}

// The channel scope is told whether the destination holds the source. One that does has nothing of it copied in again:
// an operation that writes nothing leaves whatever the destination holds. One that starts unwritten is the source's
// wherever an operation that declares nothing leaves it, and only the operation's where one declares every sample.
TEST(ProcessChannelScopeTest, TheScopeCopiesNothingIntoADestinationThatHoldsTheSource)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source =
        colourAssetWithCard(s.session(), 5u, 3u, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return 0.1f * static_cast<float>(c + 1u);
        });
    ASSERT_TRUE(source.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);

    /// An operation that writes nothing, or every sample, and says which.
    class StillOperation final : public an::ChannelScopeOperation
    {
    public:
        explicit StillOperation(bool writesEverySample) : m_writesEverySample(writesEverySample) {}

        [[nodiscard]] an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                                     const an::ChannelScopeOutput& output) override
        {
            return m_writesEverySample ? scalePlane(input, output, 2.0) : an::Status::success();
        }

        [[nodiscard]] bool writesEveryOutputSample() const noexcept override
        {
            return m_writesEverySample;
        }

    private:
        bool m_writesEverySample = false;
    };

    // A copy of the source, then marked where the scope would copy over it again.
    an::Result<an::ui::ProcessDestination> held =
        an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
    ASSERT_TRUE(held.hasValue());
    ASSERT_TRUE(held->holdsSource());
    {
        std::optional<an::ImageWriteAccess<float>> access = held->asset().write<float>();
        ASSERT_TRUE(access.has_value());
        *access->view().sampleAt(1u, 2u, 1u, 0u) = -5.0f;
    }
    StillOperation nothing(false);
    ASSERT_TRUE(
        an::ui::applyProcessChannelScope(scope->execution(), an::ChannelScope::Whole, processSource, *held, nothing)
            .hasValue());
    an::Result<an::consumer::ImageAsset> kept = held->release();
    ASSERT_TRUE(kept.hasValue());
    EXPECT_FLOAT_EQ(assetSample(*kept, 2u, 1u, 1u), -5.0f)
        << "the scope copied the source over a destination holding it";

    an::Result<an::ui::ProcessDestination> unwritten =
        an::ui::ProcessDestination::unwrittenFor(s.session(), scope->engine(), processSource);
    ASSERT_TRUE(unwritten.hasValue());
    ASSERT_TRUE(an::ui::applyProcessChannelScope(scope->execution(), an::ChannelScope::Whole, processSource, *unwritten,
                                                 nothing)
                    .hasValue());
    an::Result<an::consumer::ImageAsset> copied = unwritten->release();
    ASSERT_TRUE(copied.hasValue());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_FLOAT_EQ(assetSample(*copied, 4u, 2u, c), 0.1f * static_cast<float>(c + 1u));
    }

    an::Result<an::ui::ProcessDestination> covered =
        an::ui::ProcessDestination::unwrittenFor(s.session(), scope->engine(), processSource);
    ASSERT_TRUE(covered.hasValue());
    StillOperation everySample(true);
    ASSERT_TRUE(an::ui::applyProcessChannelScope(scope->execution(), an::ChannelScope::Whole, processSource, *covered,
                                                 everySample)
                    .hasValue());
    an::Result<an::consumer::ImageAsset> scaled = covered->release();
    ASSERT_TRUE(scaled.hasValue());
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_FLOAT_EQ(assetSample(*scaled, 4u, 2u, c), 0.2f * static_cast<float>(c + 1u));
    }
}

TEST(ProcessChannelChoicesTest, OneTableNamesEveryChoiceOnceInTheSelectorsOrder)
{
    const std::span<const an::ui::ProcessChannelChoice> choices = an::ui::processChannelChoices();
    ASSERT_EQ(choices.size(), 21u);
    std::set<std::string> labels;
    std::set<int> scopes;
    int groups = 0;
    for (const an::ui::ProcessChannelChoice& choice : choices)
    {
        EXPECT_TRUE(labels.insert(choice.label).second) << choice.label;
        EXPECT_TRUE(scopes.insert(static_cast<int>(choice.scope)).second) << choice.label;
        EXPECT_EQ(an::ui::processChannelScopeForLabel(QString::fromLatin1(choice.label)), choice.scope);
        EXPECT_EQ(an::ui::processChannelLabel(choice.scope), QString::fromLatin1(choice.label));
        groups += choice.separatorBefore ? 1 : 0;
    }
    EXPECT_EQ(groups, 5);
    // The processes compare a selector's text with these labels, so each is held as it is spelled.
    const std::vector<std::pair<an::ChannelScope, std::string>> expected{
        {an::ChannelScope::Whole, "RGB"},
        {an::ChannelScope::Red, "Red"},
        {an::ChannelScope::Green, "Green"},
        {an::ChannelScope::Blue, "Blue"},
        {an::ChannelScope::CieLightness, "CIE L* (Lightness)"},
        {an::ChannelScope::CieA, "CIE a*"},
        {an::ChannelScope::CieB, "CIE b*"},
        {an::ChannelScope::CieChroma, "CIE c*"},
        {an::ChannelScope::CieHue, "CIE h*"},
        {an::ChannelScope::CieX, "CIE X"},
        {an::ChannelScope::CieY, "CIE Y (Luminance)"},
        {an::ChannelScope::CieZ, "CIE Z"},
        {an::ChannelScope::HslHue, "HSL Hue"},
        {an::ChannelScope::HslSaturation, "HSL Saturation"},
        {an::ChannelScope::HslLightness, "HSL Lightness"},
        {an::ChannelScope::HsvHue, "HSV Hue"},
        {an::ChannelScope::HsvSaturation, "HSV Saturation"},
        {an::ChannelScope::HsvValue, "HSV Value"},
        {an::ChannelScope::YCbCrLuma, "YCbCr Luma"},
        {an::ChannelScope::YCbCrBlueChroma, "YCbCr Blue Chroma"},
        {an::ChannelScope::YCbCrRedChroma, "YCbCr Red Chroma"},
    };
    ASSERT_EQ(expected.size(), choices.size());
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_EQ(choices[index].scope, expected[index].first) << index;
        EXPECT_EQ(std::string(choices[index].label), expected[index].second) << index;
    }
    EXPECT_EQ(an::ui::processChannelScopeForLabel(QStringLiteral("Grayscale")), an::ChannelScope::Whole);
    EXPECT_FALSE(an::ui::processChannelScopeForLabel(QStringLiteral("Luminance")).has_value());
    // HSI is the engine's and no process selector offers it.
    EXPECT_TRUE(an::ui::processChannelLabel(an::ChannelScope::HsiIntensity).isEmpty());
}

TEST(ProcessChannelScopeTest, AStoredChannelIsTheOnlyOneTheOperationWrites)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source =
        colourAssetWithCard(s.session(), 4u, 3u, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return 0.1f * static_cast<float>(c + 1u);
        });
    ASSERT_TRUE(source.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    ASSERT_TRUE(lease.hasValue());
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);

    for (const auto& [scopeChoice, written] : std::vector<std::pair<an::ChannelScope, std::uint32_t>>{
             {an::ChannelScope::Red, 0u}, {an::ChannelScope::Green, 1u}, {an::ChannelScope::Blue, 2u}})
    {
        an::Result<an::ui::ProcessDestination> destination =
            an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
        ASSERT_TRUE(destination.hasValue());
        FillOperation fill(0.9);
        const an::Result<an::ChannelScopeResult> ran =
            an::ui::applyProcessChannelScope(scope->execution(), scopeChoice, processSource, *destination, fill);
        ASSERT_TRUE(ran.hasValue()) << ran.status().message();
        EXPECT_EQ(fill.calls, 1);
        EXPECT_EQ(fill.planeChannels, 1u);
        an::Result<an::consumer::ImageAsset> result = destination->release();
        ASSERT_TRUE(result.hasValue());
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            EXPECT_FLOAT_EQ(assetSample(*result, 2u, 1u, c), c == written ? 0.9f : 0.1f * static_cast<float>(c + 1u))
                << an::channelScopeName(scopeChoice) << " channel " << c;
        }
    }
}

TEST(ProcessChannelScopeTest, AComponentOfAnotherEncodingKeepsTheSourcesCards)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source =
        colourAssetWithCard(s.session(), 4u, 3u, [](std::uint32_t, std::uint32_t, std::uint32_t c) {
            return 0.2f + 0.1f * static_cast<float>(c);
        });
    ASSERT_TRUE(source.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
    ASSERT_TRUE(destination.hasValue());

    // A CIE, HSL, HSV or YCbCr channel is recomposed into an image that keeps the source's metadata.
    FillOperation lightness(0.5);
    const an::Result<an::ChannelScopeResult> ran = an::ui::applyProcessChannelScope(
        scope->execution(), an::ChannelScope::CieLightness, processSource, *destination, lightness);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    an::Result<an::consumer::ImageAsset> result = destination->release();
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(cardKeywordsOf(*result), (std::set<std::string>{"OBJECT"}));
    EXPECT_EQ(result->descriptor(), source->descriptor());
    // The lightness changed: every channel moved.
    EXPECT_NE(assetSample(*result, 0u, 0u, 0u), assetSample(*source, 0u, 0u, 0u));
}

// A colour component is converted from fractions of full scale, and every file the product reads states none: an
// integer one as sensor codes and a floating one as a physical quantity. A component of such an image is taken from its
// samples as fractions of full scale - as the view shows them - so it is the component of the same samples described
// as normalized, and the result keeps the image's own description.
TEST(ProcessChannelScopeTest, AComponentOfAnImageWithNoFullScaleIsTakenAsFractionsOfFullScale)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    // The result of a lightness set to one half over an asset, and that result's description.
    const auto lightnessHalved = [&](const an::consumer::ImageAsset& asset) -> std::optional<an::consumer::ImageAsset> {
        an::Result<an::consumer::AssetReadLease> lease = asset.read();
        EXPECT_TRUE(lease.hasValue());
        if (!lease.hasValue())
        {
            return std::nullopt;
        }
        const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
        an::Result<an::ui::ProcessDestination> destination =
            an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
        EXPECT_TRUE(destination.hasValue());
        if (!destination.hasValue())
        {
            return std::nullopt;
        }
        FillOperation half(0.5);
        const an::Result<an::ChannelScopeResult> ran = an::ui::applyProcessChannelScope(
            scope->execution(), an::ChannelScope::CieLightness, processSource, *destination, half);
        EXPECT_TRUE(ran.hasValue()) << ran.status().message();
        if (!ran.hasValue())
        {
            return std::nullopt;
        }
        an::Result<an::consumer::ImageAsset> result = destination->release();
        EXPECT_TRUE(result.hasValue());
        return result.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*result)) : std::nullopt;
    };
    const auto expectTheSameComponent = [&](const an::consumer::ImageAsset& stated,
                                            const an::consumer::ImageAsset& normalized, const char* what) {
        SCOPED_TRACE(what);
        const std::optional<an::consumer::ImageAsset> fromStated = lightnessHalved(stated);
        const std::optional<an::consumer::ImageAsset> fromNormalized = lightnessHalved(normalized);
        ASSERT_TRUE(fromStated.has_value() && fromNormalized.has_value());
        EXPECT_EQ(fromStated->descriptor(), stated.descriptor());
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            for (std::uint32_t y = 0u; y < 3u; ++y)
            {
                for (std::uint32_t x = 0u; x < 4u; ++x)
                {
                    EXPECT_EQ(assetSample(*fromStated, x, y, c), assetSample(*fromNormalized, x, y, c))
                        << x << ',' << y << ',' << c;
                }
            }
        }
        EXPECT_NE(assetSample(*fromStated, 1u, 1u, 0u), assetSample(stated, 1u, 1u, 0u)) << "the lightness moved";
    };
    const std::function<std::uint16_t(std::uint32_t, std::uint32_t, std::uint32_t)> codes =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(9000u + 7000u * c + 900u * x + 400u * y);
        };
    const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)> fractions =
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return 0.15f + 0.1f * static_cast<float>(c) + 0.02f * static_cast<float>(x + y);
        };
    std::optional<an::consumer::ImageAsset> sensorCodes =
        makeAsset<std::uint16_t>(s.session(), 4u, 3u, Channels::Rgb, codes, an::NumericDomain::RawSensorCode);
    std::optional<an::consumer::ImageAsset> normalizedCodes =
        makeAsset<std::uint16_t>(s.session(), 4u, 3u, Channels::Rgb, codes, an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(sensorCodes.has_value() && normalizedCodes.has_value());
    expectTheSameComponent(*sensorCodes, *normalizedCodes, "sixteen-bit sensor codes");
    for (const an::NumericDomain domain : {an::NumericDomain::PhysicalScaled, an::NumericDomain::RawSensorCode})
    {
        std::optional<an::consumer::ImageAsset> stated =
            makeAsset<float>(s.session(), 4u, 3u, Channels::Rgb, fractions, domain);
        std::optional<an::consumer::ImageAsset> normalized =
            makeAsset<float>(s.session(), 4u, 3u, Channels::Rgb, fractions, an::NumericDomain::NormalizedLinear);
        ASSERT_TRUE(stated.has_value() && normalized.has_value());
        expectTheSameComponent(*stated, *normalized,
                               domain == an::NumericDomain::PhysicalScaled ? "a floating physical quantity"
                                                                           : "floating sensor codes");
    }
}

TEST(ProcessChannelScopeTest, AGreyImageIsRunWholeWhateverTheChoice)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::ui::ImageDocument> document =
        greyDocument(s.session(), 4u, 4u, [](std::uint32_t x, std::uint32_t) { return 0.1f * static_cast<float>(x); });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = document->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
    FillOperation fill(0.75);
    const an::Result<an::ChannelScopeResult> ran = an::ui::applyProcessChannelScope(
        scope->execution(), an::ChannelScope::HsvValue, processSource, *destination, fill);
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->scope, an::ChannelScope::Whole);
    an::Result<an::consumer::ImageAsset> result = destination->release();
    EXPECT_FLOAT_EQ(assetSample(*result, 3u, 3u), 0.75f);
    EXPECT_EQ(an::ui::effectiveProcessChannel(an::ChannelScope::Red, *document->descriptor()), an::ChannelScope::Whole);
}

TEST(ProcessChannelScopeTest, ADestinationNoLongerDescribedAsTheSourceIsRefused)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::ui::ImageDocument> document =
        greyDocument(s.session(), 4u, 4u, [](std::uint32_t, std::uint32_t) { return 0.1f; });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = document->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
    std::optional<an::consumer::ImageAsset> smaller = makeAsset<float>(
        s.session(), 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(destination->replace(std::move(*smaller)).ok());
    FillOperation fill(0.75);
    const an::Result<an::ChannelScopeResult> ran = an::ui::applyProcessChannelScope(
        scope->execution(), an::ChannelScope::Whole, processSource, *destination, fill);
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(fill.calls, 0);
}

namespace
{

/// Blends a result of `resultValue` over a source of `sourceValue` and answers the first sample.
template <typename SampleT>
[[nodiscard]] double blendedSample(const an::consumer::EngineSession& session, SampleT sourceValue, SampleT resultValue,
                                   const an::ui::ProcessBlendRequest& request, an::Status* status = nullptr)
{
    std::optional<an::consumer::ImageAsset> source = makeAsset<SampleT>(
        session, 4u, 2u, Channels::Grey, [&](std::uint32_t, std::uint32_t, std::uint32_t) { return sourceValue; });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope->engine(), scope->execution(), processSource);
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = destination->asset().write<SampleT>();
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (SampleT& sample : *row)
            {
                sample = resultValue;
            }
        }
    }
    const an::Status blended =
        an::ui::blendProcessResult(scope->engine(), scope->execution(), processSource, *destination, request);
    if (status != nullptr)
    {
        *status = blended;
    }
    an::Result<an::consumer::ImageAsset> result = destination->release();
    return assetSample(*result, 1u, 1u);
}

} // namespace

TEST(ProcessBlendTest, ModulationMovesTheSourceTowardTheResultByItsWeight)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    EXPECT_NEAR(blendedSample<float>(s.session(), 0.2f, 0.6f, {.modulation = 0.25}), 0.3, 1e-6);
    EXPECT_NEAR(blendedSample<float>(s.session(), 0.2f, 0.6f, {.modulation = 1.0}), 0.6, 1e-6);
    EXPECT_NEAR(blendedSample<float>(s.session(), 0.2f, 0.6f, {.modulation = 0.0}), 0.2, 1e-6);
    // Code values blend as code values: an affine weight of two codes is the same weight of their fractions.
    EXPECT_EQ(blendedSample<std::uint16_t>(s.session(), 1000u, 3000u, {.modulation = 0.5}), 2000.0);
    EXPECT_EQ(blendedSample<double>(s.session(), 0.25, 0.75, {.modulation = 0.5}), 0.5);
}

TEST(ProcessBlendTest, AModulationOutsideTheUnitIntervalIsRefused)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    an::Status status;
    (void)blendedSample<float>(s.session(), 0.2f, 0.6f, {.modulation = 1.5}, &status);
    EXPECT_EQ(status.code(), an::ErrorCode::OutOfRange);
}

namespace
{

/// Blends a result of 0.8 over a source of 0.2 through a mask whose samples are `mask(x, y)`, and answers the first
/// row of the result.
[[nodiscard]] std::vector<double> maskedRow(const an::consumer::EngineSession& session,
                                            const std::function<float(std::uint32_t)>& mask, bool invert,
                                            double modulation = 1.0)
{
    std::optional<an::consumer::ImageAsset> maskAsset = makeAsset<float>(
        session, 4u, 2u, Channels::Grey, [&](std::uint32_t x, std::uint32_t, std::uint32_t) { return mask(x); });
    an::Result<an::consumer::AssetReadLease> maskLease = maskAsset->read();
    std::optional<an::consumer::ImageAsset> source = makeAsset<float>(
        session, 4u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.2f; });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope->engine(), scope->execution(), processSource);
    {
        std::optional<an::ImageWriteAccess<float>> access = destination->asset().write<float>();
        for (std::uint32_t y = 0u; y < 2u; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (float& sample : *row)
            {
                sample = 0.8f;
            }
        }
    }
    const an::Status blended =
        an::ui::blendProcessResult(scope->engine(), scope->execution(), processSource, *destination,
                                   {.modulation = modulation, .mask = &maskLease->image(), .invertMask = invert});
    EXPECT_TRUE(blended.ok()) << blended.message();
    an::Result<an::consumer::ImageAsset> result = destination->release();
    std::vector<double> row;
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        row.push_back(assetSample(*result, x, 0u));
    }
    return row;
}

} // namespace

TEST(ProcessBlendTest, AMaskWeighsTheResultPixelByPixel)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    const auto mask = [](std::uint32_t x) { return static_cast<float>(x) / 3.0f; };
    const std::vector<double> row = maskedRow(s.session(), mask, false);
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        EXPECT_NEAR(row[x], 0.2 + 0.6 * (static_cast<double>(x) / 3.0), 1e-6) << x;
    }
    const std::vector<double> inverted = maskedRow(s.session(), mask, true);
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        EXPECT_NEAR(inverted[x], 0.2 + 0.6 * (1.0 - static_cast<double>(x) / 3.0), 1e-6) << x;
    }
    // With a modulation as well: the source moved toward the result by the modulation times the mask.
    const std::vector<double> modulated = maskedRow(s.session(), mask, false, 0.5);
    for (std::uint32_t x = 0u; x < 4u; ++x)
    {
        EXPECT_NEAR(modulated[x], 0.2 + 0.6 * 0.5 * (static_cast<double>(x) / 3.0), 1e-6) << x;
    }
}

TEST(ProcessBlendTest, AOneChannelMaskWeighsEveryChannelOfAColourResult)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> maskAsset =
        makeAsset<std::uint8_t>(s.session(), 2u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x == 0u ? 0u : 255u);
        });
    an::Result<an::consumer::AssetReadLease> maskLease = maskAsset->read();
    std::optional<an::consumer::ImageAsset> source = makeAsset<float>(
        s.session(), 2u, 1u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.1f; });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = source->read();
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(s.session(), scope->engine(), scope->execution(), processSource);
    {
        std::optional<an::ImageWriteAccess<float>> access = destination->asset().write<float>();
        for (std::uint32_t c = 0u; c < 3u; ++c)
        {
            const auto row = access->view().row(c, 0u);
            for (float& sample : *row)
            {
                sample = 0.9f;
            }
        }
    }
    // A UInt8 mask over a Float32 result is converted at full scale: 255 weighs one.
    ASSERT_TRUE(an::ui::blendProcessResult(scope->engine(), scope->execution(), processSource, *destination,
                                           {.mask = &maskLease->image()})
                    .ok());
    an::Result<an::consumer::ImageAsset> result = destination->release();
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(assetSample(*result, 0u, 0u, c), 0.1, 1e-6) << c;
        EXPECT_NEAR(assetSample(*result, 1u, 0u, c), 0.9, 1e-6) << c;
    }
}

TEST(ProcessBlendTest, AMaskOfAnotherSizeIsRefusedAndTheResultLeftAlone)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> maskAsset = makeAsset<float>(
        s.session(), 3u, 3u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    an::Result<an::consumer::AssetReadLease> maskLease = maskAsset->read();
    an::Status status;
    const double sample = blendedSample<float>(s.session(), 0.2f, 0.6f, {.mask = &maskLease->image()}, &status);
    EXPECT_EQ(status.code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_NEAR(sample, 0.6, 1e-6);
}

TEST(ProcessRegionTest, TheCropHoldsTheRegionsOwnSamples)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::ui::ImageDocument> document = greyDocument(
        s.session(), 6u, 5u, [](std::uint32_t x, std::uint32_t y) { return static_cast<float>(x + 10u * y); });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> lease = document->read();
    an::Result<an::consumer::ImageAsset> crop = an::ui::cropProcessRegion(
        s.session(), scope->engine(), scope->execution(), lease->image(), lease->validity(), QRect(2, 1, 3, 2));
    ASSERT_TRUE(crop.hasValue()) << crop.status().message();
    ASSERT_TRUE(crop->descriptor().has_value());
    EXPECT_EQ(crop->descriptor()->extent().width(), 3u);
    EXPECT_EQ(crop->descriptor()->extent().height(), 2u);
    EXPECT_FLOAT_EQ(assetSample(*crop, 0u, 0u), 12.0f);
    EXPECT_FLOAT_EQ(assetSample(*crop, 2u, 1u), 24.0f);

    an::Result<an::consumer::ImageAsset> outside = an::ui::cropProcessRegion(
        s.session(), scope->engine(), scope->execution(), lease->image(), lease->validity(), QRect(5, 4, 3, 3));
    ASSERT_FALSE(outside.hasValue());
    EXPECT_EQ(outside.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(ProcessRegionTest, TheCompositePastesThePatchIntoACopyOfTheBase)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> base =
        makeAsset<float>(s.session(), 6u, 5u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<float>(x + 10u * y);
        });
    std::optional<an::consumer::ImageAsset> patch = makeAsset<float>(
        s.session(), 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return -1.0f; });
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    an::Result<an::consumer::AssetReadLease> baseLease = base->read();
    an::Result<an::consumer::AssetReadLease> patchLease = patch->read();
    an::Result<an::consumer::ImageAsset> composite =
        an::ui::composeProcessRegion(s.session(), scope->engine(), scope->execution(), baseLease->image(),
                                     patchLease->image(), patchLease->validity(), QPoint(3, 2));
    ASSERT_TRUE(composite.hasValue()) << composite.status().message();
    EXPECT_FLOAT_EQ(assetSample(*composite, 3u, 2u), -1.0f);
    EXPECT_FLOAT_EQ(assetSample(*composite, 4u, 3u), -1.0f);
    EXPECT_FLOAT_EQ(assetSample(*composite, 2u, 2u), 22.0f);
    EXPECT_FLOAT_EQ(assetSample(*composite, 5u, 2u), 25.0f);
    EXPECT_FLOAT_EQ(assetSample(*composite, 3u, 4u), 43.0f);
    // The base is the base still: every composite is a copy of it.
    EXPECT_FLOAT_EQ(assetSample(*base, 3u, 2u), 23.0f);
    EXPECT_NE(composite->revision().assetIdentity, base->revision().assetIdentity);

    an::Result<an::consumer::ImageAsset> overhanging =
        an::ui::composeProcessRegion(s.session(), scope->engine(), scope->execution(), baseLease->image(),
                                     patchLease->image(), patchLease->validity(), QPoint(5, 4));
    ASSERT_FALSE(overhanging.hasValue());
    EXPECT_EQ(overhanging.status().code(), an::ErrorCode::IncompatibleArguments);
}

TEST(ProcessRegionTest, TheBaseTakesThePatchesRepresentationAndChannels)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());

    // A grey UInt16 image under colour Float32 patches: its plane in every channel, at full scale.
    std::optional<an::consumer::ImageAsset> grey = makeAsset<std::uint16_t>(
        s.session(), 4u, 3u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 65535u; });
    std::optional<an::consumer::ImageAsset> colourPatch = makeAsset<float>(
        s.session(), 2u, 2u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    an::Result<an::consumer::AssetReadLease> greyLease = grey->read();
    const std::optional<an::ImageDescriptor> colourBase = an::ui::processCompositeBaseDescriptor(
        *colourPatch->descriptor(), *grey->descriptor(), scope->engine().limits());
    ASSERT_TRUE(colourBase.has_value());
    EXPECT_EQ(colourBase->extent().width(), 4u);
    EXPECT_EQ(colourBase->extent().channelCount(), 3u);
    an::Result<an::consumer::ImageAsset> promoted = an::ui::buildProcessCompositeBase(
        s.session(), scope->engine(), scope->execution(), greyLease->image(), greyLease->validity(), *colourBase);
    ASSERT_TRUE(promoted.hasValue()) << promoted.status().message();
    EXPECT_EQ(promoted->descriptor(), colourBase);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_FLOAT_EQ(assetSample(*promoted, 3u, 2u, c), 1.0f) << c;
    }

    // A colour image under grey patches: its luminance, with the Rec. 601 weights.
    std::optional<an::consumer::ImageAsset> colour =
        makeAsset<float>(s.session(), 4u, 3u, Channels::Rgb,
                         [](std::uint32_t, std::uint32_t, std::uint32_t c) { return c == 0u ? 1.0f : 0.0f; });
    std::optional<an::consumer::ImageAsset> greyPatch = makeAsset<float>(
        s.session(), 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    an::Result<an::consumer::AssetReadLease> colourLease = colour->read();
    const std::optional<an::ImageDescriptor> greyBase = an::ui::processCompositeBaseDescriptor(
        *greyPatch->descriptor(), *colour->descriptor(), scope->engine().limits());
    ASSERT_TRUE(greyBase.has_value());
    an::Result<an::consumer::ImageAsset> reduced = an::ui::buildProcessCompositeBase(
        s.session(), scope->engine(), scope->execution(), colourLease->image(), colourLease->validity(), *greyBase);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    EXPECT_NEAR(assetSample(*reduced, 0u, 0u), 0.2989, 1e-6);
}

namespace
{

/// An operation that raises every sample of its Float32 plane to a power, writing every one.
class PowerOperation final : public an::ChannelScopeOperation
{
public:
    explicit PowerOperation(bool declaresEverySample = false) : m_declares(declaresEverySample) {}

    [[nodiscard]] an::Status run(const an::ExecutionContext&, const an::ChannelScopeInput& input,
                                 const an::ChannelScopeOutput& output) override
    {
        ++calls;
        if (input.descriptor->sampleType() != an::SampleType::Float32)
        {
            return an::Status::failure(an::ErrorCode::Unsupported, "a Float32 plane is expected");
        }
        const an::ImageLayout& in = *input.layout;
        const an::ImageLayout& out = *output.layout;
        for (std::uint32_t y = 0u; y < in.extent().height(); ++y)
        {
            const auto* from =
                reinterpret_cast<const float*>(static_cast<const std::byte*>(input.samples) + *in.rowByteOffset(0u, y));
            auto* to = reinterpret_cast<float*>(static_cast<std::byte*>(output.samples) + *out.rowByteOffset(0u, y));
            for (std::uint32_t x = 0u; x < in.extent().width(); ++x)
            {
                to[x] = static_cast<float>(std::pow((std::max)(static_cast<double>(from[x]), 0.0), 1.0 / 2.2));
            }
        }
        return an::Status::success();
    }

    [[nodiscard]] bool writesEveryOutputSample() const noexcept override
    {
        return m_declares;
    }

    int calls = 0;

private:
    bool m_declares = false;
};

/// Every sample of an asset, every channel's in row order.
[[nodiscard]] std::vector<double> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::vector<double> samples;
    const std::optional<an::ImageDescriptor> described = asset.descriptor();
    if (!described.has_value())
    {
        return samples;
    }
    const an::ImageExtent& extent = described->extent();
    for (std::uint32_t c = 0u; c < extent.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < extent.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < extent.width(); ++x)
            {
                samples.push_back(assetSample(asset, x, y, c));
            }
        }
    }
    return samples;
}

/// The samples of an asset whose CIE lightness the framework's channel scope raised to a power, through a run's
/// conversions or none.
[[nodiscard]] std::vector<double> lightnessRaised(const an::consumer::EngineSession& session,
                                                  const an::consumer::OperationScope& scope,
                                                  const an::consumer::ImageAsset& asset,
                                                  an::ui::ProcessConversions* conversions)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return {};
    }
    const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease).reusing(conversions);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope.engine(), scope.execution(), source);
    EXPECT_TRUE(destination.hasValue());
    if (!destination.hasValue())
    {
        return {};
    }
    PowerOperation power;
    const an::Result<an::ChannelScopeResult> ran = an::ui::applyProcessChannelScope(
        scope.execution(), an::ChannelScope::CieLightness, source, *destination, power);
    EXPECT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(power.calls, 1);
    an::Result<an::consumer::ImageAsset> result = destination->release();
    return result.hasValue() ? samplesOf(*result) : std::vector<double>{};
}

/// A colour sixteen-bit image every code of which is a fraction of full scale, as an asset.
[[nodiscard]] std::optional<an::consumer::ImageAsset> colourCodes(const an::consumer::EngineSession& session)
{
    return makeAsset<std::uint16_t>(
        session, 9u, 5u, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(6000u + 5000u * c + 2300u * x + 1700u * y);
        },
        an::NumericDomain::NormalizedLinear);
}

} // namespace

// A component of another encoding is converted once for a revision of what a run reads: a run that carries
// conversions makes the forward half and keeps it, the next reads it and converts nothing, and both recompose what a
// run that keeps nothing recomposes, sample for sample. Another revision of the image reads nothing of the one before.
TEST(ProcessConversionsTest, AComponentOfAnotherEncodingIsConvertedOncePerRevisionOfWhatARunReads)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> asset = colourCodes(s.session());
    ASSERT_TRUE(asset.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());

    const std::vector<double> without = lightnessRaised(s.session(), *scope, *asset, nullptr);
    ASSERT_FALSE(without.empty());

    an::ui::ProcessConversions first(scope->engine(), {});
    EXPECT_EQ(lightnessRaised(s.session(), *scope, *asset, &first), without);
    EXPECT_EQ(first.scopesPrepared(), 1u);
    EXPECT_EQ(first.scopesReused(), 0u);
    const std::vector<an::ui::ProcessConversion> made = first.made();
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(made.front().source, asset->revision());
    ASSERT_NE(made.front().prepared, nullptr);
    EXPECT_EQ(made.front().prepared->request().scope, an::ChannelScope::CieLightness);
    EXPECT_GT(made.front().byteCount(), 0u);

    an::ui::ProcessConversions second(scope->engine(), made);
    EXPECT_EQ(lightnessRaised(s.session(), *scope, *asset, &second), without);
    EXPECT_EQ(second.scopesPrepared(), 0u);
    EXPECT_EQ(second.scopesReused(), 1u);
    EXPECT_TRUE(second.made().empty());
    ASSERT_EQ(second.reused().size(), 1u);
    EXPECT_EQ(second.reused().front().prepared, made.front().prepared);

    // The image written is another revision: what was kept of the one before is never read for it.
    {
        an::Result<an::consumer::AssetWriteLease> writer = asset->write();
        ASSERT_TRUE(writer.hasValue());
        std::optional<an::ImageWriteAccess<std::uint16_t>> access = writer->write<std::uint16_t>();
        ASSERT_TRUE(access.has_value());
        (*access->view().row(1u, 2u))[3] = 60000u;
    }
    ASSERT_NE(asset->revision(), made.front().source);
    const std::vector<double> rewritten = lightnessRaised(s.session(), *scope, *asset, nullptr);
    an::ui::ProcessConversions third(scope->engine(), made);
    EXPECT_EQ(lightnessRaised(s.session(), *scope, *asset, &third), rewritten);
    EXPECT_EQ(third.scopesReused(), 0u);
    EXPECT_EQ(third.scopesPrepared(), 1u);
}

// A forward half the memory refuses is converted in the call instead, as a run that keeps nothing converts it: the run
// recomposes the same samples and keeps nothing for the runs that follow.
TEST(ProcessConversionsTest, AForwardHalfTheMemoryRefusesIsConvertedInTheCall)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> asset = makeAsset<std::uint16_t>(
        s.session(), 32u, 16u, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(6000u + 5000u * c + 600u * x + 1500u * y);
        },
        an::NumericDomain::NormalizedLinear);
    ASSERT_TRUE(asset.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    const std::vector<double> without = lightnessRaised(s.session(), *scope, *asset, nullptr);
    ASSERT_FALSE(without.empty());

    // Three Float32 planes of 32 by 16 take more than this context's budget.
    an::EngineContextOptions small;
    small.maxConcurrency = 2u;
    small.limits.memoryBudgetBytes = 4096u;
    an::Result<an::EngineContext> starved = an::EngineContext::create(small);
    ASSERT_TRUE(starved.hasValue()) << starved.status().message();
    an::ui::ProcessConversions refused(*starved, {});
    EXPECT_EQ(lightnessRaised(s.session(), *scope, *asset, &refused), without);
    EXPECT_EQ(refused.scopesPrepared(), 0u);
    EXPECT_EQ(refused.scopesReused(), 0u);
    EXPECT_TRUE(refused.made().empty());
}

namespace
{

/// The samples of a UInt16 result of 40000 blended over a source of 8000 through a Float32 gradient mask, through a
/// run's conversions or none.
[[nodiscard]] std::vector<double> blendedThroughMask(const an::consumer::EngineSession& session,
                                                     const an::consumer::OperationScope& scope,
                                                     const an::consumer::ImageAsset& source,
                                                     const an::consumer::ImageAsset& mask,
                                                     an::ui::ProcessConversions* conversions)
{
    an::Result<an::consumer::AssetReadLease> maskLease = mask.read();
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    EXPECT_TRUE(maskLease.hasValue() && lease.hasValue());
    if (!maskLease.hasValue() || !lease.hasValue())
    {
        return {};
    }
    const an::ui::ProcessSource processSource = an::ui::ProcessSource::of(*lease).reusing(conversions);
    an::Result<an::ui::ProcessDestination> destination =
        an::ui::ProcessDestination::copyOf(session, scope.engine(), scope.execution(), processSource);
    EXPECT_TRUE(destination.hasValue());
    if (!destination.hasValue())
    {
        return {};
    }
    {
        std::optional<an::ImageWriteAccess<std::uint16_t>> access = destination->asset().write<std::uint16_t>();
        for (std::uint32_t y = 0u; y < 4u; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (std::uint16_t& sample : *row)
            {
                sample = 40000u;
            }
        }
    }
    const an::Status blended = an::ui::blendProcessResult(
        scope.engine(), scope.execution(), processSource, *destination,
        {.mask = &maskLease->image(), .maskRevision = maskLease->revision(), .invertMask = false});
    EXPECT_TRUE(blended.ok()) << blended.message();
    an::Result<an::consumer::ImageAsset> result = destination->release();
    return result.hasValue() ? samplesOf(*result) : std::vector<double>{};
}

} // namespace

// A mask of another representation than the result is converted into it once for a revision of the mask: the first
// run keeps the conversion, the next reads it, and the blend is the one a run that keeps nothing makes.
TEST(ProcessConversionsTest, AMaskOfAnotherRepresentationIsConvertedOncePerRevisionOfTheMask)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::ImageAsset> source = makeAsset<std::uint16_t>(
        s.session(), 6u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 8000u; });
    std::optional<an::consumer::ImageAsset> mask =
        makeAsset<float>(s.session(), 6u, 4u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<float>(x) / 5.0f; });
    ASSERT_TRUE(source.has_value() && mask.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());

    const std::vector<double> without = blendedThroughMask(s.session(), *scope, *source, *mask, nullptr);
    ASSERT_FALSE(without.empty());
    EXPECT_NE(without.front(), without.back()) << "the mask weighs the result pixel by pixel";

    an::ui::ProcessConversions first(scope->engine(), {});
    EXPECT_EQ(blendedThroughMask(s.session(), *scope, *source, *mask, &first), without);
    EXPECT_EQ(first.planarMade(), 1u);
    EXPECT_EQ(first.planarReused(), 0u);
    const std::vector<an::ui::ProcessConversion> made = first.made();
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(made.front().source, mask->revision());
    ASSERT_NE(made.front().planar, nullptr);
    EXPECT_EQ(an::consumer::sampleTypeOf(*made.front().planar), an::SampleType::UInt16);

    an::ui::ProcessConversions second(scope->engine(), made);
    EXPECT_EQ(blendedThroughMask(s.session(), *scope, *source, *mask, &second), without);
    EXPECT_EQ(second.planarMade(), 0u);
    EXPECT_EQ(second.planarReused(), 1u);

    // The mask written is another revision: what was kept of the one before is never read for it.
    {
        an::Result<an::consumer::AssetWriteLease> writer = mask->write();
        ASSERT_TRUE(writer.hasValue());
        std::optional<an::ImageWriteAccess<float>> access = writer->write<float>();
        ASSERT_TRUE(access.has_value());
        (*access->view().row(0u, 1u))[2] = 1.0f;
    }
    ASSERT_NE(mask->revision(), made.front().source);
    const std::vector<double> rewritten = blendedThroughMask(s.session(), *scope, *source, *mask, nullptr);
    EXPECT_NE(rewritten, without) << "the written sample weighs its pixel otherwise";
    an::ui::ProcessConversions fourth(scope->engine(), made);
    EXPECT_EQ(blendedThroughMask(s.session(), *scope, *source, *mask, &fourth), rewritten);
    EXPECT_EQ(fourth.planarReused(), 0u);
    EXPECT_EQ(fourth.planarMade(), 1u);

    // A mask in the result's representation is read where it lies, and nothing is kept of it.
    std::optional<an::consumer::ImageAsset> codes = makeAsset<std::uint16_t>(
        s.session(), 6u, 4u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t, std::uint32_t) { return static_cast<std::uint16_t>(x * 13107u); });
    ASSERT_TRUE(codes.has_value());
    an::ui::ProcessConversions third(scope->engine(), {});
    EXPECT_EQ(blendedThroughMask(s.session(), *scope, *source, *codes, &third),
              blendedThroughMask(s.session(), *scope, *source, *codes, nullptr));
    EXPECT_TRUE(third.made().empty());
}

namespace
{

/// A planar Float32 image of one sample as a conversion kept for a revision; or one holding nothing.
[[nodiscard]] an::ui::ProcessConversion planarConversion(const an::consumer::EngineSession& session,
                                                         an::consumer::AssetRevision source,
                                                         an::SampleType type = an::SampleType::Float32)
{
    an::ui::ProcessConversion conversion{.source = source};
    const auto keepCopyOf = [&conversion](const an::consumer::ImageAsset& made) {
        an::Result<an::consumer::AssetReadLease> lease = made.read();
        ASSERT_TRUE(lease.hasValue());
        std::visit(
            [&conversion](const auto& typed) {
                auto copy = typed.clone();
                ASSERT_TRUE(copy.hasValue());
                conversion.planar = std::make_shared<const an::consumer::AnyImage>(std::move(*copy));
            },
            lease->image());
    };
    if (type == an::SampleType::Float32)
    {
        std::optional<an::consumer::ImageAsset> made = makeAsset<float>(
            session, 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
        keepCopyOf(*made);
    }
    else
    {
        std::optional<an::consumer::ImageAsset> made = makeAsset<std::uint16_t>(
            session, 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 7u; });
        keepCopyOf(*made);
    }
    return conversion;
}

/// A forward half of a scope over a small colour image as a conversion kept for a revision.
[[nodiscard]] an::ui::ProcessConversion preparedConversion(const an::consumer::EngineSession& session,
                                                           const an::consumer::OperationScope& scope,
                                                           an::consumer::AssetRevision source, an::ChannelScope channel)
{
    an::ui::ProcessConversion conversion{.source = source};
    std::optional<an::consumer::ImageAsset> image = colourCodes(session);
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    const std::optional<an::ImageDescriptor> described = image->descriptor();
    const std::optional<an::ImageDescriptor> component = an::ui::componentReadingOf(*described);
    an::Result<an::ChannelScopePlan> plan =
        an::planChannelScope(an::ui::processChannelScopeRequest(channel, *component));
    const auto& typed = std::get<an::Image<std::uint16_t>>(lease->image());
    const an::ImageLayout layout = typed.view()->layout();
    const an::ChannelScopeSource scoped{.descriptor = &*component, .samples = typed.view()->data(), .layout = &layout};
    an::Result<an::ChannelScopePreparation> prepared =
        an::prepareChannelScope(scope.engine(), scope.execution(), *plan, scoped);
    EXPECT_TRUE(prepared.hasValue()) << prepared.status().message();
    if (prepared.hasValue())
    {
        conversion.prepared = std::make_shared<const an::ChannelScopePreparation>(std::move(*prepared));
    }
    return conversion;
}

/// A revision of an asset.
[[nodiscard]] an::consumer::AssetRevision revisionOf(std::uint64_t identity, std::uint64_t pixels)
{
    return an::consumer::AssetRevision{.assetIdentity = identity, .pixelGeneration = pixels};
}

} // namespace

// A dialog keeps what its runs made up to its capacity, the least recently used let go first - what a run read counts
// as used - and none of an asset's revision older than the newest it keeps of it, which no run can read again.
TEST(ProcessConversionCacheTest, KeepsTheLatestRevisionOfAnAssetUpToItsCapacity)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    an::ui::ProcessConversionCache cache(3u);
    const an::ui::ProcessConversion first = planarConversion(s.session(), revisionOf(1u, 1u));
    const an::ui::ProcessConversion second = planarConversion(s.session(), revisionOf(2u, 1u));
    const an::ui::ProcessConversion third = planarConversion(s.session(), revisionOf(3u, 1u));
    cache.keep({first, second, third});
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_GT(cache.byteCount(), 0u);

    // What a run read is as recently used as what it made: the second, read, outlives the first.
    const an::ui::ProcessConversion fourth = planarConversion(s.session(), revisionOf(4u, 1u));
    cache.keep({fourth}, {first});
    cache.keep({planarConversion(s.session(), revisionOf(5u, 1u))});
    std::vector<an::consumer::AssetRevision> kept;
    for (const an::ui::ProcessConversion& conversion : cache.kept())
    {
        kept.push_back(conversion.source);
    }
    EXPECT_EQ(kept,
              (std::vector<an::consumer::AssetRevision>{revisionOf(1u, 1u), revisionOf(4u, 1u), revisionOf(5u, 1u)}));

    // A newer revision of an asset lets the older go, and an older one is not kept beside a newer.
    cache.keep({planarConversion(s.session(), revisionOf(4u, 2u))});
    cache.keep({planarConversion(s.session(), revisionOf(4u, 1u))});
    kept.clear();
    for (const an::ui::ProcessConversion& conversion : cache.kept())
    {
        kept.push_back(conversion.source);
    }
    EXPECT_EQ(kept,
              (std::vector<an::consumer::AssetRevision>{revisionOf(1u, 1u), revisionOf(5u, 1u), revisionOf(4u, 2u)}));

    // Conversions that hold nothing, or are of no asset, are not kept; clearing lets every one go.
    cache.keep({an::ui::ProcessConversion{.source = revisionOf(9u, 1u)},
                planarConversion(s.session(), an::consumer::AssetRevision{})});
    EXPECT_EQ(cache.size(), 3u);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.byteCount(), 0u);
}

// One revision keeps one forward half - a change of the dialog's channel lets the last one go - and one image of each
// representation, the latest made.
TEST(ProcessConversionCacheTest, KeepsOneForwardHalfAndOneImageOfEachRepresentationOfARevision)
{
    Session s;
    ASSERT_TRUE(s.provider.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(s.session());
    ASSERT_TRUE(scope.has_value());
    an::ui::ProcessConversionCache cache(8u);
    const an::consumer::AssetRevision revision = revisionOf(7u, 3u);
    const an::ui::ProcessConversion lightness =
        preparedConversion(s.session(), *scope, revision, an::ChannelScope::CieLightness);
    const an::ui::ProcessConversion value =
        preparedConversion(s.session(), *scope, revision, an::ChannelScope::HsvValue);
    ASSERT_TRUE(lightness.prepared && value.prepared);
    cache.keep({lightness});
    cache.keep({value});
    std::vector<an::ui::ProcessConversion> kept = cache.kept();
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_EQ(kept.front().prepared, value.prepared);

    const an::ui::ProcessConversion floats = planarConversion(s.session(), revision);
    const an::ui::ProcessConversion codes = planarConversion(s.session(), revision, an::SampleType::UInt16);
    const an::ui::ProcessConversion floatsAgain = planarConversion(s.session(), revision);
    cache.keep({floats, codes, floatsAgain});
    kept = cache.kept();
    ASSERT_EQ(kept.size(), 3u);
    EXPECT_EQ(kept[0].prepared, value.prepared);
    EXPECT_EQ(kept[1].planar, codes.planar);
    EXPECT_EQ(kept[2].planar, floatsAgain.planar);

    // A run reads what it is handed and what it kept itself.
    an::ui::ProcessConversions run(scope->engine(), kept);
    EXPECT_EQ(run.prepared(revision, value.prepared->request()), value.prepared);
    EXPECT_EQ(run.prepared(revision, lightness.prepared->request()), nullptr);
    EXPECT_EQ(run.prepared(revisionOf(7u, 4u), value.prepared->request()), nullptr);
    EXPECT_EQ(run.planar(revision, an::SampleType::UInt16), codes.planar);
    EXPECT_EQ(run.planar(revision, an::SampleType::Int16), nullptr);
    run.keep(lightness);
    EXPECT_EQ(run.prepared(revision, lightness.prepared->request()), lightness.prepared);
    EXPECT_EQ(run.scopesReused(), 2u);
    EXPECT_EQ(run.planarReused(), 1u);
    EXPECT_EQ(run.made().size(), 1u);
}
