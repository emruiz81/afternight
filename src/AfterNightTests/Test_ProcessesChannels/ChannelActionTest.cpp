// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The thirty-nine channel and conversion actions over a still image, each run as the image menu runs it: what each
/// opens and beside which view, under which names and history, in which representation, with what of the image's
/// metadata and validity, and what each says - and publishes - when it cannot run.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using process_utils::ChannelAction;
using process_utils::ChannelActionKind;

/// The scene an action is run over: a colour scene of another representation than a conversion's target.
[[nodiscard]] const char* sceneFor(const ChannelAction& action)
{
    return action.kind == ChannelActionKind::Convert && action.target == an::SampleType::UInt16 ? "scene-rgb-float32"
                                                                                                : "scene-rgb-uint16";
}

/// What the view of output i is called after the source's.
[[nodiscard]] std::string suffixOf(const ChannelAction& action, std::size_t index)
{
    return action.kind == ChannelActionKind::Extract ? action.outputs[index].suffix : action.suffix;
}

/// What the history of the view of output i begins with, for a source of a name.
[[nodiscard]] std::string historyOf(const ChannelAction& action, std::size_t index, const std::string& name)
{
    if (action.kind == ChannelActionKind::Convert)
    {
        return std::string("Converted to ") + action.label + " from image '" + name + "'";
    }
    const char* label = action.kind == ChannelActionKind::Extract ? action.outputs[index].label : action.label;
    return std::string(label) + " extracted from image '" + name + "'";
}

/// The metadata of what an asset holds.
[[nodiscard]] an::ImageMetadata metadataOf(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return an::ImageMetadata{};
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
}

/// The description of what an asset holds.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit([](const auto& typed) { return typed.descriptor(); }, lease->image());
}

/// A copy of an asset's image under other metadata, beside an explicit mask when one is given.
[[nodiscard]] std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const an::ImageMetadata& metadata,
                                                                   std::optional<an::ValidityMask> mask = std::nullopt)
{
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    EXPECT_TRUE(lease.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!lease.hasValue() || !scope.has_value())
    {
        return std::nullopt;
    }
    std::optional<an::consumer::AnyImage> copy = std::visit(
        [&](const auto& typed) -> std::optional<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!view.has_value() || !described.has_value())
            {
                return std::nullopt;
            }
            an::Result<an::Image<SampleT>> image =
                an::Image<SampleT>::create(scope->engine(), *described, an::StorageArrangement::Planar,
                                           an::StorageInitialization::Zeroed, metadata);
            EXPECT_TRUE(image.hasValue()) << image.status().message();
            if (!image.hasValue())
            {
                return std::nullopt;
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
                an::ConversionCounters counters;
                const an::Status copied =
                    an::convertImageSamples(scope->execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                            view->layout(), an::kSampleTypeOf<SampleT>, access->view().data(),
                                            access->view().layout(), an::ConversionPolicy{}, counters);
                EXPECT_TRUE(copied.ok()) << copied.message();
            }
            return an::consumer::AnyImage(std::move(*image));
        },
        lease->image());
    lease->release();
    scope.reset();
    if (!copy.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, std::move(*copy), std::move(mask));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

/// The metadata a well-described colour image carries: its object and exposure, a linear meaning, a solution, and the
/// cards a file stated - the object, and a linear processing-state card.
[[nodiscard]] an::ImageMetadata describedMetadata()
{
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.objectName = "M 42", .exposureSeconds = 120.0});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(
        an::AstrometricSolutionRequest{.referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39},
                                       .referencePixelX = 6.0,
                                       .referencePixelY = 4.0,
                                       .pixelScaleArcsecPerPixel = 2.4,
                                       .orientationDegrees = 17.0,
                                       .mirrored = false,
                                       .hasDistortion = false});
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "target");
    an::Result<an::CodecCard> linear = an::CodecCard::create(an::CodecNamespace::Fits, "ANLIN", "'linear'", "");
    EXPECT_TRUE(object.hasValue() && linear.hasValue());
    an::Result<an::CodecMetadata> cards =
        an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{*object, *linear});
    EXPECT_TRUE(cards.hasValue()) << cards.status().message();
    an::ImageSemanticsRequest meaning;
    meaning.transferFunction = an::TransferFunction::Linear;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    return an::ImageMetadata::create(*acquisition, *semantics, *solution, an::Provenance{}, *cards, std::nullopt);
}

/// A colour scene under the described metadata, as a view made active.
[[nodiscard]] ViewHandle describedSceneView(ProcessTestContext& context, std::string_view scene)
{
    ViewHandle plain = sceneView(context.engineSession(), scene);
    if (plain == nullptr)
    {
        return ViewHandle();
    }
    const std::shared_ptr<const an::consumer::ImageAsset> asset = plain->document().asset();
    std::optional<an::consumer::ImageAsset> described =
        withMetadata(context.engineSession(), *asset, describedMetadata());
    dropView(context, plain);
    if (!described.has_value())
    {
        return ViewHandle();
    }
    ViewHandle view = assetView(std::move(*described), "scene");
    activate(context, *view);
    return view;
}

[[nodiscard]] bool hasCard(const an::ImageMetadata& metadata, const char* keyword)
{
    for (const an::CodecCard& card : metadata.codec().cards())
    {
        if (card.keyword() == keyword)
        {
            return true;
        }
    }
    return false;
}

/// Whether an action's outputs are the image's stored channels.
[[nodiscard]] bool storesChannels(const ChannelAction& action)
{
    return action.kind == ChannelActionKind::Extract &&
           an::channelScopeEncoding(action.outputs[0].scope) == an::ChannelSchema::Rgb;
}

class ChannelActionTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const ChannelAction& action() const
    {
        return process_utils::channelActions()[GetParam()];
    }

    [[nodiscard]] ProcessTestContext& context()
    {
        return *m_context;
    }

    /// Runs the action on the active view and waits for the views it opens.
    [[nodiscard]] std::vector<ImageViewControl*> runAndAwait(std::size_t count)
    {
        const std::size_t before = m_context->madeViews.size();
        const int messages = static_cast<int>(m_context->messages.size());
        runRegisteredAction(action().action);
        return awaitNewViews(*m_context, before, count, messages);
    }

private:
    shared::RegistrySnapshot m_snapshot;
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

std::string actionName(const ::testing::TestParamInfo<std::size_t>& info)
{
    std::string name = process_utils::channelActions()[info.param].action;
    return name.substr(std::string("action").size());
}

} // namespace

// image: the action opens one view per image it makes, beside the image, named after it, its history saying what was
// extracted or converted from it, holding what the table makes of the image; the image itself is left as it was and
// nothing is said.
TEST_P(ChannelActionTest, ItOpensWhatItMakesBesideTheImageNamedAfterIt)
{
    ViewHandle target = sceneView(context().engineSession(), sceneFor(action()), "scene");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    const an::ui::DocumentRevision revision = target->document().revision();
    const std::size_t count = process_utils::channelActionImageCount(action());
    const std::vector<ImageViewControl*> views = runAndAwait(count);
    ASSERT_EQ(views.size(), count) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_TRUE(context().messages.isEmpty()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<fixtures::FixtureSamples> expected =
        expectedOutputs(context().engineSession(), *target->document().asset(), action());
    ASSERT_EQ(expected.size(), count);
    for (std::size_t index = 0u; index < count; ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(views[index]->getUniqueName(), "scene" + suffixOf(action(), index));
        EXPECT_EQ(views[index]->document().names().sourcePath, QString());
        EXPECT_EQ(madeHistoryOf(context(), views[index]), historyOf(action(), index, "scene"));
        EXPECT_EQ(madeNeighbourOf(context(), views[index]), target.get());
        EXPECT_FALSE(views[index]->isSequence());
        const std::optional<fixtures::FixtureSamples> made = documentSamples(views[index]->document());
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(largestDifference(*made, expected[index]), 0.0);
    }
    EXPECT_EQ(target->document().revision(), revision) << "the image the action read is left as it was";
    dropView(context(), target);
}

// representation: a stored channel keeps the image's representation; a colour component, and the a*b* view, is
// Float32, Float64 for a Float64 image; a conversion is its target - to the image's own representation, a copy.
TEST_P(ChannelActionTest, EachImageItMakesIsInTheRepresentationItsRowStates)
{
    struct Source
    {
        an::SampleType type;
        double scale;
    };
    for (const Source source : {Source{an::SampleType::UInt8, 255.0}, Source{an::SampleType::UInt16, 65535.0},
                                Source{an::SampleType::Int16, 1.0}, Source{an::SampleType::Float32, 1.0},
                                Source{an::SampleType::Float64, 1.0}})
    {
        SCOPED_TRACE(an::sampleTypeName(source.type));
        std::optional<an::ui::ImageDocument> document;
        (void)an::dispatchSampleType(source.type, [&]<typename SampleT>() {
            document = makeDocument<SampleT>(context().engineSession(), 6u, 4u, Channels::Rgb,
                                             [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                                 const double fraction = 0.1 + 0.03 * x + 0.05 * y + 0.2 * c;
                                                 if constexpr (std::is_same_v<SampleT, std::int16_t>)
                                                 {
                                                     return static_cast<SampleT>(fraction * 30000.0 - 12000.0);
                                                 }
                                                 else
                                                 {
                                                     return static_cast<SampleT>(fraction * source.scale);
                                                 }
                                             });
        });
        ASSERT_TRUE(document.has_value());
        ViewHandle target = makeTargetView("scene", std::move(*document));
        activate(context(), *target);
        const std::size_t count = process_utils::channelActionImageCount(action());
        const std::vector<ImageViewControl*> views = runAndAwait(count);
        ASSERT_EQ(views.size(), count) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        for (ImageViewControl* view : views)
        {
            const std::optional<an::ImageDescriptor> described = view->imageDescriptor();
            ASSERT_TRUE(described.has_value());
            switch (action().kind)
            {
            case ChannelActionKind::Convert:
                EXPECT_EQ(described->sampleType(), action().target);
                EXPECT_EQ(described->extent().channelCount(), 3u);
                break;
            case ChannelActionKind::ChromaView:
                EXPECT_EQ(described->sampleType(),
                          source.type == an::SampleType::Float64 ? an::SampleType::Float64 : an::SampleType::Float32);
                EXPECT_EQ(described->extent().channelCount(), 3u);
                break;
            case ChannelActionKind::Extract:
                EXPECT_EQ(described->extent().channelCount(), 1u);
                EXPECT_EQ(described->sampleType(), storesChannels(action())                 ? source.type
                                                   : source.type == an::SampleType::Float64 ? an::SampleType::Float64
                                                                                            : an::SampleType::Float32);
                break;
            }
            EXPECT_EQ(described->extent().width(), 6u);
            EXPECT_EQ(described->extent().height(), 4u);
        }
        dropView(context(), target);
    }
}

// metadata: a stored channel and a conversion keep everything the image states - its cards, its acquisition record,
// its solution and its meaning; a colour component and the a*b* view keep the acquisition record and the solution and
// drop the cards, which describe the colour image; every image's lineage names the engine operation, and none carries
// an output decision.
TEST_P(ChannelActionTest, ItKeepsWhatTheImageStatesAsItsRowSays)
{
    ViewHandle target = describedSceneView(context(), sceneFor(action()));
    ASSERT_NE(target, nullptr);
    const an::ImageMetadata source = metadataOf(*target->document().asset());
    const std::size_t count = process_utils::channelActionImageCount(action());
    const std::vector<ImageViewControl*> views = runAndAwait(count);
    ASSERT_EQ(views.size(), count) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const bool keepsCards = action().kind == ChannelActionKind::Convert || storesChannels(action());
    for (ImageViewControl* view : views)
    {
        const an::ImageMetadata made = metadataOf(*view->document().asset());
        EXPECT_EQ(made.acquisition(), source.acquisition());
        EXPECT_EQ(made.astrometry(), source.astrometry());
        EXPECT_TRUE(made.astrometry().present());
        EXPECT_EQ(hasCard(made, "OBJECT"), keepsCards);
        EXPECT_EQ(hasCard(made, "ANLIN"), keepsCards);
        EXPECT_FALSE(made.outputIntent().has_value());
        if (keepsCards)
        {
            EXPECT_EQ(made.semantics(), source.semantics());
        }
        else
        {
            EXPECT_EQ(made.semantics().transferFunction(), an::TransferFunction::Unspecified);
            EXPECT_TRUE(made.semantics().validity().isAllValid());
        }
        ASSERT_FALSE(made.provenance().entries().empty());
        EXPECT_EQ(made.provenance().entries().back().operation(),
                  action().kind == ChannelActionKind::Convert ? "an::convertImageSamples" : "an::extractChannelScope");
    }
    dropView(context(), target);
}

// error: an extraction of a grey image says it is not a colour image, in a message titled by the action, and opens
// nothing; a conversion of one converts it.
TEST_P(ChannelActionTest, OnAGreyImageAnExtractionSaysWhyAndOpensNothing)
{
    ViewHandle target = sceneView(context().engineSession(), "scene-mono-float32", "grey");
    ASSERT_NE(target, nullptr);
    activate(context(), *target);
    if (action().kind == ChannelActionKind::Convert)
    {
        const std::vector<ImageViewControl*> views = runAndAwait(1u);
        ASSERT_EQ(views.size(), 1u) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        EXPECT_EQ(views.front()->imageDescriptor()->extent().channelCount(), 1u);
        dropView(context(), target);
        return;
    }
    const int before = static_cast<int>(context().messages.size());
    const std::size_t made = context().madeViews.size();
    runRegisteredAction(action().action);
    ASSERT_TRUE(awaitMessage(context(), before));
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Extract channel"));
    EXPECT_TRUE(context().errorTexts.back().startsWith(QStringLiteral("Could not extract ")))
        << context().errorTexts.back().toStdString();
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("not a colour image")))
        << context().errorTexts.back().toStdString();
    EXPECT_EQ(context().madeViews.size(), made);
    dropView(context(), target);
}

INSTANTIATE_TEST_SUITE_P(ThirtyNineActions, ChannelActionTest,
                         ::testing::Range<std::size_t>(0u, process_utils::channelActions().size()), actionName);

// A view the application cannot make is reported, and the action opens nothing more.
TEST(ChannelActionFailureTest, AViewThatCannotBeMadeIsReportedAndNothingMoreOpens)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = sceneView(context->engineSession(), "scene-rgb-float32", "scene");
    ASSERT_NE(target, nullptr);
    activate(*context, *target);
    context->refuseViews = true;
    const int before = static_cast<int>(context->messages.size());
    runRegisteredAction("actionExtract_RGB_Channels");
    ASSERT_TRUE(awaitMessage(*context, before));
    EXPECT_EQ(context->messages.size(), before + 1);
    EXPECT_EQ(context->messages.back(), QStringLiteral("error:Extract channel"));
    EXPECT_TRUE(context->errorTexts.back().contains(QStringLiteral("scene - RED")))
        << context->errorTexts.back().toStdString();
    EXPECT_TRUE(context->madeViews.empty());
    dropView(*context, target);
}

// validity: a stored channel keeps the image's rule and its own plane of an explicit mask; a component of a pixel the
// image's validity rejects holds no number, and the component says so by its rule.
TEST(ChannelActionValidityTest, AStoredChannelKeepsItsPlaneOfTheMaskAndAComponentMarksWhatHadNoColour)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle plain = sceneView(context->engineSession(), "scene-rgb-float32", "plain");
    ASSERT_NE(plain, nullptr);
    const std::shared_ptr<const an::consumer::ImageAsset> asset = plain->document().asset();
    const std::optional<an::ImageDescriptor> described = descriptorOf(*asset);
    ASSERT_TRUE(described.has_value());
    // One plane per channel: the green channel of pixel (3, 2) means nothing.
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(scope.has_value());
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(scope->engine(), described->extent());
    ASSERT_TRUE(mask.hasValue()) << mask.status().message();
    ASSERT_TRUE(mask->view()->setValid(1u, 3u, 2u, false));
    scope.reset();
    an::ImageMetadata metadata = metadataOf(*asset);
    an::ImageSemanticsRequest meaning = metadata.semantics().toRequest();
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue());
    const an::ImageMetadata masked =
        an::ImageMetadata::create(metadata.acquisition(), *semantics, metadata.astrometry(), metadata.provenance(),
                                  metadata.codec(), std::nullopt);
    std::optional<an::consumer::ImageAsset> maskedAsset =
        withMetadata(context->engineSession(), *asset, masked, std::move(*mask));
    ASSERT_TRUE(maskedAsset.has_value());
    dropView(*context, plain);
    ViewHandle target = assetView(std::move(*maskedAsset), "scene");
    activate(*context, *target);

    std::size_t before = context->madeViews.size();
    runRegisteredAction("actionExtract_Green_Channel");
    std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
    ASSERT_EQ(views.size(), 1u);
    {
        an::Result<an::consumer::AssetReadLease> green = views.front()->document().read();
        ASSERT_TRUE(green.hasValue());
        const an::ImageMetadata greenMetadata =
            std::visit([](const auto& typed) { return typed.metadata(); }, green->image());
        EXPECT_EQ(greenMetadata.semantics().validity().rule(), an::ValidityRule::ExplicitMask);
        ASSERT_NE(green->validity(), nullptr);
        const auto bits = green->validity()->view();
        ASSERT_TRUE(bits.has_value());
        EXPECT_EQ(bits->planeCount(), 1u);
        EXPECT_EQ(bits->test(0u, 3u, 2u), std::optional<bool>(false));
        EXPECT_EQ(bits->test(0u, 4u, 2u), std::optional<bool>(true));
    }

    before = context->madeViews.size();
    runRegisteredAction("actionExtract_CIE_L");
    views = awaitNewViews(*context, before, 1u, 0);
    ASSERT_EQ(views.size(), 1u);
    {
        an::Result<an::consumer::AssetReadLease> lightness = views.front()->document().read();
        ASSERT_TRUE(lightness.hasValue());
        const an::ImageMetadata lightnessMetadata =
            std::visit([](const auto& typed) { return typed.metadata(); }, lightness->image());
        EXPECT_EQ(lightnessMetadata.semantics().validity().rule(), an::ValidityRule::FiniteOnly);
        EXPECT_EQ(lightness->validity(), nullptr);
    }
    EXPECT_TRUE(std::isnan(documentSample(views.front()->document(), 3u, 2u)));
    EXPECT_FALSE(std::isnan(documentSample(views.front()->document(), 4u, 2u)));
    dropView(*context, target);
}

// validity: a conversion keeps the image's rule; a value rule the conversion moves - exact zero where a signed
// representation counts from its smallest value - is carried as the mask it decides on the image's own samples.
TEST(ChannelActionValidityTest, AConversionCarriesARuleItWouldMoveAsTheMaskItDecides)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        context->engineSession(), 4u, 3u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(x == 1u && y == 1u ? 0u : 1000u * (x + y + 1u));
        });
    ASSERT_TRUE(document.has_value());
    const std::shared_ptr<const an::consumer::ImageAsset> plain = document->asset();
    an::ImageMetadata metadata = metadataOf(*plain);
    an::ImageSemanticsRequest meaning = metadata.semantics().toRequest();
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ZeroIsNoData});
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    ASSERT_TRUE(semantics.hasValue());
    std::optional<an::consumer::ImageAsset> ruled =
        withMetadata(context->engineSession(), *plain,
                     an::ImageMetadata::create(metadata.acquisition(), *semantics, metadata.astrometry(),
                                               metadata.provenance(), metadata.codec(), std::nullopt));
    ASSERT_TRUE(ruled.has_value());
    ViewHandle target = assetView(std::move(*ruled), "zero");
    activate(*context, *target);

    for (const char* action : {"actionConvert_to_32_bit_float", "actionConvert_to_signed_16_bit"})
    {
        SCOPED_TRACE(action);
        const std::size_t before = context->madeViews.size();
        runRegisteredAction(action);
        const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
        ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        an::Result<an::consumer::AssetReadLease> converted = views.front()->document().read();
        ASSERT_TRUE(converted.hasValue());
        const an::ValidityDescriptor rule =
            std::visit([](const auto& typed) { return typed.metadata().semantics().validity(); }, converted->image());
        if (std::string(action) == "actionConvert_to_32_bit_float")
        {
            EXPECT_EQ(rule.rule(), an::ValidityRule::ZeroIsNoData) << "zero is still zero";
            EXPECT_EQ(converted->validity(), nullptr);
        }
        else
        {
            EXPECT_EQ(rule.rule(), an::ValidityRule::ExplicitMask) << "zero becomes the smallest signed code";
            ASSERT_NE(converted->validity(), nullptr);
            const auto bits = converted->validity()->view();
            ASSERT_TRUE(bits.has_value());
            EXPECT_EQ(bits->test(0u, 1u, 1u), std::optional<bool>(false));
            EXPECT_EQ(bits->test(0u, 2u, 1u), std::optional<bool>(true));
        }
    }
    dropView(*context, target);
}

// representation: a conversion rounds to the nearest code, maps the ends of one representation's range onto the
// other's - a signed one counted from its smallest code - and saturates a sample beyond them, a full-scale floating
// sample included, rather than wrapping it to the lowest code.
TEST(ChannelConversionTest, AConversionRoundsToTheNearestCodeAndMapsTheRangesEndsOntoEachOther)
{
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const std::vector<float> fractions{0.0f, 1.0f, 128.4f / 255.0f, 128.6f / 255.0f, 1.5f, -0.5f};
    std::optional<an::ui::ImageDocument> floating =
        makeDocument<float>(context->engineSession(), static_cast<std::uint32_t>(fractions.size()), 1u, Channels::Grey,
                            [&](std::uint32_t x, std::uint32_t, std::uint32_t) { return fractions[x]; });
    ASSERT_TRUE(floating.has_value());
    ViewHandle target = makeTargetView("fractions", std::move(*floating));
    activate(*context, *target);
    struct Expected
    {
        const char* action;
        std::vector<double> codes;
    };
    constexpr double kInt32Min = -2147483648.0;
    constexpr double kInt32Max = 2147483647.0;
    constexpr double kUInt32Max = 4294967295.0;
    for (const Expected& expected :
         {Expected{"actionConvert_to_8_bit", {0.0, 255.0, 128.0, 129.0, 255.0, 0.0}},
          Expected{"actionConvert_to_signed_16_bit",
                   {-32768.0, 32767.0, 128.4 / 255.0 * 65535.0 - 32768.0, 128.6 / 255.0 * 65535.0 - 32768.0, 32767.0,
                    -32768.0}},
          Expected{"actionConvert_to_signed_32_bit", {kInt32Min, kInt32Max, 0.0, 0.0, kInt32Max, kInt32Min}},
          Expected{"actionConvert_to_unsigned_32_bit", {0.0, kUInt32Max, 0.0, 0.0, kUInt32Max, 0.0}}})
    {
        SCOPED_TRACE(expected.action);
        const std::size_t before = context->madeViews.size();
        runRegisteredAction(expected.action);
        const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
        ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> converted = documentSamples(views.front()->document());
        ASSERT_TRUE(converted.has_value());
        for (std::uint32_t x : {0u, 1u, 4u, 5u})
        {
            EXPECT_EQ(converted->value(x, 0u, 0u), expected.codes[x]) << "sample " << x;
        }
        if (std::string(expected.action) == "actionConvert_to_8_bit" ||
            std::string(expected.action) == "actionConvert_to_signed_16_bit")
        {
            EXPECT_EQ(converted->value(2u, 0u, 0u), std::round(expected.codes[2]));
            EXPECT_EQ(converted->value(3u, 0u, 0u), std::round(expected.codes[3]));
        }
    }
    dropView(*context, target);

    std::optional<an::ui::ImageDocument> signedCodes = makeDocument<std::int16_t>(
        context->engineSession(), 3u, 1u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::int16_t>(x == 0u ? -32768 : x == 1u ? 32767 : 0);
        });
    ASSERT_TRUE(signedCodes.has_value());
    ViewHandle codes = makeTargetView("codes", std::move(*signedCodes));
    activate(*context, *codes);
    const std::size_t before = context->madeViews.size();
    runRegisteredAction("actionConvert_to_64_bit_float");
    const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, 0);
    ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> floats = documentSamples(views.front()->document());
    ASSERT_TRUE(floats.has_value());
    EXPECT_EQ(floats->value(0u, 0u, 0u), 0.0) << "the smallest signed code is the bottom of the range";
    EXPECT_EQ(floats->value(1u, 0u, 0u), 1.0) << "the largest is the top";
    EXPECT_NEAR(floats->value(2u, 0u, 0u), 32768.0 / 65535.0, 1.0e-15);
    dropView(*context, codes);
}
