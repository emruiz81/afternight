// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a preview's frames convert of what they read once and then read again, against the Controls library: the
/// forward conversion of a component of another encoding, kept for the revision of the image a frame runs over - the
/// target, or the reduction an interactive frame runs over - and the mask laid out in the result's representation, kept
/// for the mask's revision; neither made again until the image it was made from changes, and every frame what the
/// conversion in the call makes. And the preview area: its crop cut once per revision and area, and an interactive
/// frame over it shown as its patch painted over what the view paints, its histogram derived from counts, no composite
/// of the whole image made - until the settled frame composes it.

#include "Test_ImageRTPreviewControl/SliderProcessSupport.h"

#include "controls/ImageViewScrollArea.h"
#include "session/PreviewProxies.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramComputation.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QScrollBar>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::frames;

/// The admission every computation of a case's expectations runs under.
[[nodiscard]] std::optional<an::consumer::OperationScope> admitted(const an::consumer::EngineSession& session)
{
    an::Result<an::consumer::OperationScope> scope =
        session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*scope);
}

/// Every sample of an image read under a lease, every channel's in row order, as doubles.
[[nodiscard]] std::vector<double> samplesOf(const an::consumer::AssetReadLease& lease)
{
    std::vector<double> samples;
    lease.visit([&samples](const auto& typed) {
        const auto view = typed.view();
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        if (!view.has_value() || !described.has_value())
        {
            return;
        }
        for (std::uint32_t channel = 0u; channel < described->extent().channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < described->extent().height(); ++y)
            {
                for (std::uint32_t x = 0u; x < described->extent().width(); ++x)
                {
                    samples.push_back(static_cast<double>(*view->sampleAt(channel, x, y, 0u)));
                }
            }
        }
    });
    return samples;
}

/// Every sample of what a document holds now.
[[nodiscard]] std::vector<double> documentSamples(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue());
    return lease.hasValue() ? samplesOf(*lease) : std::vector<double>{};
}

/// Turns the event loop until the window has shown more interactive frames than it had.
[[nodiscard]] bool interactiveFramesShown(const SliderProcess& process, const ProcessRunTelemetry& before,
                                          std::uint64_t more = 1u)
{
    return pumpUntil([&process, &before, more]() {
        return process.runTelemetry().interactivePreviewsCommitted >= before.interactivePreviewsCommitted + more;
    });
}

/// Applies the process to its target and waits for the target to show it.
void applyAndSettle(SliderProcess& process, ImageViewControl& target)
{
    QAbstractButton* apply = process.buttons->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    apply->setEnabled(true);
    apply->click();
    ASSERT_TRUE(settleSlider(process));
    ASSERT_TRUE(settleView(target));
}

/// What the slider process's body makes of a sixteen-bit sample at a value: `scalePlane`'s own conversion.
[[nodiscard]] std::uint16_t scaled16(double sample, int value)
{
    return an::convertSample<double, std::uint16_t>(sample * (1.0 + static_cast<double>(value) / 100.0),
                                                    an::ConversionPolicy{.domain = an::DomainMapping::CodeValue})
        .value;
}

/// The display counts of an asset, counted directly.
[[nodiscard]] std::optional<an::ui::HistogramDisplayData> countOf(const an::consumer::EngineSession& session,
                                                                  const an::consumer::ImageAsset& asset)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!scope.has_value() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::HistogramDisplayData> counted =
        an::ui::computeHistogramDisplayData(scope->execution(), *lease, an::ui::HistogramResolution::Bits8);
    EXPECT_TRUE(counted.hasValue()) << counted.status().message();
    return counted.hasValue() ? std::optional<an::ui::HistogramDisplayData>(std::move(*counted)) : std::nullopt;
}

/// Holds the counts a dialog's histogram shows to those expected, channel by channel.
void expectShown(const an::ui::IPreviewHistogram& histogram, const an::ui::HistogramDisplayData& expected,
                 const char* what)
{
    auto* widget = dynamic_cast<HistogramWidget*>(const_cast<an::ui::IPreviewHistogram&>(histogram).widget());
    ASSERT_NE(widget, nullptr);
    const std::shared_ptr<const an::ui::HistogramDisplayData> shown = widget->getHistogram();
    ASSERT_NE(shown, nullptr) << what;
    ASSERT_EQ(shown->channels.size(), expected.channels.size()) << what;
    for (std::size_t channel = 0u; channel < expected.channels.size(); ++channel)
    {
        EXPECT_EQ(shown->channels[channel].bins, expected.channels[channel].bins) << what << ", channel " << channel;
        EXPECT_EQ(shown->channels[channel].clippedLow, expected.channels[channel].clippedLow) << what;
        EXPECT_EQ(shown->channels[channel].clippedHigh, expected.channels[channel].clippedHigh) << what;
        EXPECT_EQ(shown->channels[channel].samples, expected.channels[channel].samples) << what;
    }
}

/// The sixteen-bit target with a part of it scaled as the slider process's body scales it at a value, as a new asset.
[[nodiscard]] std::optional<an::consumer::ImageAsset> scaledWithin(const an::consumer::EngineSession& session,
                                                                   const an::ui::ImageDocument& target,
                                                                   const QRect& part, int value)
{
    const std::vector<double> samples = documentSamples(target);
    const std::optional<an::ImageDescriptor> described = target.descriptor();
    if (!described.has_value())
    {
        return std::nullopt;
    }
    const std::uint32_t width = described->extent().width();
    const std::uint32_t height = described->extent().height();
    return makeAsset<std::uint16_t>(
        session, width, height, Channels::Rgb, [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const double sample = samples[(static_cast<std::size_t>(c) * height + y) * width + x];
            return part.contains(static_cast<int>(x), static_cast<int>(y)) ? scaled16(sample, value)
                                                                           : static_cast<std::uint16_t>(sample);
        });
}

/// A grey Float32 gradient as a mask view, from no weight at the left edge to all of it at the right.
[[nodiscard]] ViewHandle gradientMask(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        makeDocument<float>(session, 640u, 480u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<float>(x) / 639.0f;
        });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("mask", std::move(*document));
}

/// A 640 by 480 Float32 colour target view of a gradient whose value rule calls a sample valid only when it is finite,
/// one of its samples not a number; null when it cannot be made.
[[nodiscard]] ViewHandle targetWithASampleOfNoValue(const an::consumer::EngineSession& session)
{
    std::optional<an::consumer::ImageAsset> asset =
        makeAsset<float>(session, 640u, 480u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return x == 400u && y == 300u && c == 1u ? std::numeric_limits<float>::quiet_NaN()
                                                     : static_cast<float>((x + 2u * y + 37u * c) % 251u) / 250.0f;
        });
    if (!asset.has_value())
    {
        return ViewHandle();
    }
    {
        an::Result<an::consumer::AssetWriteLease> writer = asset->write();
        if (!writer.hasValue())
        {
            return ViewHandle();
        }
        an::ImageSemanticsRequest meaning =
            std::visit([](const auto& typed) { return typed.metadata().semantics().toRequest(); }, writer->image());
        meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
        const an::Result<an::ProvenanceEntry> record = an::ProvenanceEntry::create("calibrated", "");
        if (!semantics.hasValue() || !record.hasValue() ||
            writer->transition(an::MetadataTransition{.semantics = *semantics, .record = *record}).failed())
        {
            return ViewHandle();
        }
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset), {});
    if (!document.hasValue())
    {
        return ViewHandle();
    }
    return makeTargetView("finite", std::move(*document));
}

} // namespace

// A frame on a component of another encoding converts the target into that encoding once: the frame the choice of the
// channel asked for makes the conversion, and every frame after it over the same revision reads it - and recomposes
// what the Apply, which converts in the call, writes. An Apply makes the target another image, which is converted anew.
TEST(PreviewConversions, AFrameOnAComponentOfAnotherEncodingReadsTheConversionAFrameBeforeMade)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.channels = true});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry before = process->runTelemetry();
    EXPECT_EQ(before.scopesPrepared, 0u) << "the whole image converts nothing";

    ASSERT_TRUE(process->chooseChannel(an::ChannelScope::CieLightness));
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry chosen = process->runTelemetry();
    EXPECT_EQ(chosen.scopesPrepared - before.scopesPrepared, 1u);

    process->spinBox->setValue(20);
    ASSERT_TRUE(settleSlider(*process));
    process->spinBox->setValue(35);
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_GE(after.settledPreviewsCommitted - chosen.settledPreviewsCommitted, 2u);
    EXPECT_EQ(after.scopesPrepared, chosen.scopesPrepared) << "a frame converted the target again";
    EXPECT_EQ(after.scopesReused - chosen.scopesReused, 2u);
    const std::vector<SliderRun> runs = runsFrom(process->runs(), 0u);
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs.back().value, 35);

    const std::vector<double> preview = documentSamples(process->previewView()->document());
    applyAndSettle(*process, *target);
    EXPECT_EQ(documentSamples(target->document()), preview) << "the frame is not what the Apply writes";
    // The Apply made the target another image, whose selector offers its channels again from the whole image.
    const ProcessRunTelemetry applied = process->runTelemetry();
    ASSERT_TRUE(process->chooseChannel(an::ChannelScope::CieLightness));
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().scopesPrepared - applied.scopesPrepared, 1u)
        << "a frame over the image the Apply made read the conversion of the image the target was";
}

// A masked frame converts a mask of another representation than the result once: every frame after the first over the
// same revision of the mask reads the conversion, and blends what the Apply blends. A mask changed is converted anew.
TEST(PreviewConversions, AMaskedFrameReadsTheMaskAFrameBeforeConvertedIntoTheResultsRepresentation)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle mask = gradientMask(session);
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(mask, nullptr);
    ASSERT_NE(target, nullptr);
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(target->isMasked() && target->isMaskEnabled());
    ASSERT_TRUE(settleView(*target));
    {
        SliderHandle process = openSliderProcess(*target, SliderProcessOptions{});
        process->previewCheckbox->setChecked(true);
        process->show();
        ASSERT_TRUE(settleSlider(*process));
        const ProcessRunTelemetry first = process->runTelemetry();
        EXPECT_EQ(first.planarConversionsMade, 1u) << "the opening frame converted the Float32 mask";

        process->spinBox->setValue(25);
        ASSERT_TRUE(settleSlider(*process));
        process->spinBox->setValue(45);
        ASSERT_TRUE(settleSlider(*process));
        const ProcessRunTelemetry after = process->runTelemetry();
        EXPECT_EQ(after.planarConversionsMade, first.planarConversionsMade) << "a frame converted the mask again";
        EXPECT_EQ(after.planarConversionsReused - first.planarConversionsReused, 2u);

        const std::vector<double> preview = documentSamples(process->previewView()->document());
        applyAndSettle(*process, *target);
        EXPECT_EQ(documentSamples(target->document()), preview) << "the masked frame is not what the Apply blends";

        // Another mask is another revision, converted anew.
        const ProcessRunTelemetry applied = process->runTelemetry();
        std::optional<an::consumer::ImageAsset> asset =
            makeAsset<float>(session, 640u, 480u, Channels::Grey, [](std::uint32_t, std::uint32_t y, std::uint32_t) {
                return static_cast<float>(y) / 479.0f;
            });
        ASSERT_TRUE(asset.has_value());
        mask->replaceImage(std::move(*asset));
        ASSERT_TRUE(settleView(*mask));
        ASSERT_TRUE(settleSlider(*process));
        EXPECT_GT(process->runTelemetry().planarConversionsMade, applied.planarConversionsMade);
    }
    target->removeCurrentMask();
    EXPECT_TRUE(settleView(*target));
}

// The choice of a component of another encoding in a reducible process's dialog is answered by an interactive frame
// over the target reduced, then a settled one over the target: each converts what it reads once, and the frames of a
// drag after them - over the same reduction - and the settled frame that ends it read those conversions and make none.
TEST(PreviewConversions, InteractiveFramesOverAReductionReadTheConversionTheFirstOfThemMade)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.decimated = true, .channels = true});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    const ProcessRunTelemetry start = process->runTelemetry();
    ASSERT_TRUE(process->chooseChannel(an::ChannelScope::CieLightness));
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry before = process->runTelemetry();
    EXPECT_EQ(before.viewFramesCommitted - start.viewFramesCommitted, 1u);
    EXPECT_EQ(before.viewFrameFactor, 4u);
    EXPECT_EQ(before.scopesPrepared - start.scopesPrepared, 2u) << "the reduction and the target, once each";

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFramesShown(*process, before));
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFramesShown(*process, before, 2u));
    process->slider->setValue(50);
    ASSERT_TRUE(interactiveFramesShown(*process, before, 3u));
    const ProcessRunTelemetry dragged = process->runTelemetry();
    EXPECT_EQ(dragged.viewFramesCommitted - before.viewFramesCommitted, 3u);
    EXPECT_EQ(dragged.scopesPrepared, before.scopesPrepared) << "a frame of the drag converted the reduction again";
    EXPECT_GE(dragged.scopesReused - before.scopesReused, 3u);

    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry settled = process->runTelemetry();
    EXPECT_GT(settled.settledPreviewsCommitted, dragged.settledPreviewsCommitted);
    EXPECT_EQ(settled.scopesPrepared, dragged.scopesPrepared) << "the settled frame converted the target again";
    EXPECT_GT(settled.scopesReused, dragged.scopesReused);
}

// An interactive frame over the preview area runs over the area alone, shows its patch painted over what the view
// paints, and makes no composite of the whole image: the preview's document is as the last settled frame left it, and
// the dialog's histogram, derived from counts, is exactly the count of the composite the frame shows. The frames after
// the first run over the crop it cut. The settled frame that follows the release composes the whole image, and the
// layer goes with the presentation of what it composed.
TEST(PreviewArea, AnInteractiveFrameOverThePreviewAreaIsShownAsItsPatchOverWhatTheViewPaints)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openSliderProcess(*target, SliderProcessOptions{.histogram = true, .previewArea = QSize(96, 64)});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    const QRect area = process->previewArea();
    ASSERT_EQ(area.size(), QSize(96, 64));
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::shared_ptr<const an::consumer::ImageAsset> documentBefore = process->previewView()->document().asset();
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();
    const ImageViewPresentationTelemetry viewBefore = widget->presentationTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFramesShown(*process, before));
    const ProcessRunTelemetry shown = process->runTelemetry();
    EXPECT_EQ(shown.patchFramesCommitted - before.patchFramesCommitted, 1u);
    EXPECT_EQ(shown.histogramsDerived - before.histogramsDerived, 1u);
    EXPECT_EQ(widget->presentationTelemetry().layersAdopted - viewBefore.layersAdopted, 1u);
    EXPECT_EQ(widget->presentationTelemetry().presented, viewBefore.presented) << "the view presented the frame itself";
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    EXPECT_EQ(previews.front().frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews.front().width, 96u);
    EXPECT_EQ(previews.front().height, 64u);
    EXPECT_TRUE(previews.front().partOfTarget);
    EXPECT_EQ(process->previewView()->document().asset(), documentBefore)
        << "an interactive frame over the area published a composite";
    EXPECT_NE(widget->presentationLayer(), nullptr);
    EXPECT_EQ(widget->presentationLayerRegion(), area);
    std::optional<an::consumer::ImageAsset> composite = scaledWithin(session, target->document(), area, 30);
    ASSERT_TRUE(composite.has_value());
    const std::optional<an::ui::HistogramDisplayData> expected = countOf(session, *composite);
    ASSERT_TRUE(expected.has_value());
    expectShown(*process->histogram(), *expected, "the interactive frame's derived histogram");

    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFramesShown(*process, before, 2u));
    const ProcessRunTelemetry second = process->runTelemetry();
    EXPECT_GE(second.cropsReused - shown.cropsReused, 1u) << "the second frame cut the area again";
    EXPECT_EQ(second.cropsMade, shown.cropsMade);

    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry settled = process->runTelemetry();
    EXPECT_GT(settled.settledPreviewsCommitted, second.settledPreviewsCommitted);
    EXPECT_GT(settled.histogramsCounted, second.histogramsCounted) << "the settled frame counts its composite";
    EXPECT_EQ(settled.compositeBasesBuilt, 0u)
        << "a base was built for patches of the target's own description, of which a copy of the target is the base";
    EXPECT_NE(process->previewView()->document().asset(), documentBefore);
    EXPECT_EQ(widget->presentationLayer(), nullptr) << "the layer outlived the settled frame's presentation";
    std::optional<an::consumer::ImageAsset> settledComposite = scaledWithin(session, target->document(), area, 40);
    ASSERT_TRUE(settledComposite.has_value());
    an::Result<an::consumer::AssetReadLease> expectedLease = settledComposite->read();
    ASSERT_TRUE(expectedLease.hasValue());
    EXPECT_EQ(documentSamples(process->previewView()->document()), samplesOf(*expectedLease));
    const std::optional<an::ui::HistogramDisplayData> settledCounts = countOf(session, *settledComposite);
    ASSERT_TRUE(settledCounts.has_value());
    expectShown(*process->histogram(), *settledCounts, "the settled frame's histogram");
}

// A frame over the area whose patch would be stretched by its own statistics - a process that measures each result,
// shown in autostretch - composes the whole image, never a patch over another composite's stretch.
TEST(PreviewArea, AFrameWhosePatchWouldBeStretchedByItselfComposesTheWholeImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.previewArea = QSize(96, 64)});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    process->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    const std::shared_ptr<const an::consumer::ImageAsset> documentBefore = process->previewView()->document().asset();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFramesShown(*process, before));
    EXPECT_EQ(process->runTelemetry().patchFramesCommitted, before.patchFramesCommitted);
    EXPECT_NE(process->previewView()->document().asset(), documentBefore) << "the frame composed nothing";
    EXPECT_EQ(process->previewView()->getViewWidget()->presentationLayer(), nullptr);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// A frame over the area of a target whose value rule leaves some samples meaning nothing composes the whole image as
// before: what such a patch shows over the view's presentation is not what the composite shows.
TEST(PreviewArea, AFrameOverATargetWithSamplesOfNoValueComposesTheWholeImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = targetWithASampleOfNoValue(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.previewArea = QSize(96, 64)});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    const std::shared_ptr<const an::consumer::ImageAsset> documentBefore = process->previewView()->document().asset();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFramesShown(*process, before));
    EXPECT_EQ(process->runTelemetry().patchFramesCommitted, before.patchFramesCommitted);
    EXPECT_NE(process->previewView()->document().asset(), documentBefore) << "the frame composed nothing";
    EXPECT_EQ(process->previewView()->getViewWidget()->presentationLayer(), nullptr);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// Interactive frames at full scale over the region the view shows run over the crop the first of them cut, until the
// view shows another part.
TEST(PreviewArea, FramesOverTheRegionTheViewShowsRunOverTheCropTheFirstCut)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.decimated = true});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(zoomPreview(*process, 4.0));
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFramesShown(*process, before));
    const ProcessRunTelemetry first = process->runTelemetry();
    EXPECT_EQ(first.viewFramesCommitted - before.viewFramesCommitted, 1u);
    EXPECT_EQ(first.cropsMade - before.cropsMade, 1u);
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFramesShown(*process, before, 2u));
    const ProcessRunTelemetry second = process->runTelemetry();
    EXPECT_EQ(second.cropsMade, first.cropsMade) << "the second frame cut the region again";
    EXPECT_EQ(second.cropsReused - first.cropsReused, 1u);

    // The view shows another part, which the next frame cuts anew.
    QScrollBar* across = process->previewView()->getScrollArea()->horizontalScrollBar();
    across->setValue(across->maximum());
    pumpQuietly();
    process->slider->setValue(50);
    ASSERT_TRUE(interactiveFramesShown(*process, before, 3u));
    EXPECT_GE(process->runTelemetry().cropsMade - second.cropsMade, 1u)
        << "a frame over another part ran over the crop of the part shown before";
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}
