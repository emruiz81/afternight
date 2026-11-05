// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A preview's frame as one operation, against the Controls library: it processes, counts what the dialog's histogram
/// shows and presents what the preview view paints in one admission, and the view and the histogram show what it made
/// as it lands, presenting and counting nothing of their own. An interactive frame of a process that declares it may
/// (`PreviewScaling::Decimated`) runs over what the view paints of the target - the target reduced by the view's
/// factor, or the part the view shows at full scale, painted as a layer over what it paints - its source a view of the
/// target with a revision of its own, and leaves the preview's document as the last settled frame left it; its
/// histogram is the target's codes remapped
/// through what the body makes of every code, exactly what its whole result counts, or a count of the body over the
/// target sampled one pixel per block; and its preview is stretched through the target's analysis, measured once.

#include "Test_ImageRTPreviewControl/ProcessDialogSupport.h"
#include "Test_ImageRTPreviewControl/SliderProcessSupport.h"
#include "processes/GaussianBlurProcess.h"

#include "session/DisplayProxy.h"
#include "session/PreviewProxies.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramComputation.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSlider>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
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

/// Every sample of an image, every channel's in row order, as doubles.
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

/// What the slider process's body makes of a sixteen-bit sample at a value: scaled, rounded to a code, held to the
/// codes - `scalePlane`'s own conversion.
[[nodiscard]] double scaled16(double sample, int value)
{
    return static_cast<double>(
        an::convertSample<double, std::uint16_t>(sample * (1.0 + static_cast<double>(value) / 100.0),
                                                 an::ConversionPolicy{.domain = an::DomainMapping::CodeValue})
            .value);
}

/// The sixteen-bit target scaled as the slider process's body scales it at a value, as a new asset.
[[nodiscard]] std::optional<an::consumer::ImageAsset> scaledTarget(const an::consumer::EngineSession& session,
                                                                   const an::ui::ImageDocument& target, int value)
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
        session, width, height, Channels::Rgb,
        [&samples, width, height, value](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            const double sample = samples[(static_cast<std::size_t>(c) * height + y) * width + x];
            return static_cast<std::uint16_t>(scaled16(sample, value));
        });
}

/// The display counts of an asset, counted directly.
[[nodiscard]] std::optional<an::ui::HistogramDisplayData> countOf(const an::consumer::EngineSession& session,
                                                                  const an::consumer::ImageAsset& asset,
                                                                  an::ui::HistogramResolution resolution)
{
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!scope.has_value() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::HistogramDisplayData> counted =
        an::ui::computeHistogramDisplayData(scope->execution(), *lease, resolution);
    EXPECT_TRUE(counted.hasValue()) << counted.status().message();
    return counted.hasValue() ? std::optional<an::ui::HistogramDisplayData>(std::move(*counted)) : std::nullopt;
}

/// Holds the counts a dialog's histogram shows to those expected, channel by channel.
void expectShown(const an::ui::IPreviewHistogram& histogram, const an::ui::HistogramDisplayData& expected)
{
    auto* widget = dynamic_cast<HistogramWidget*>(const_cast<an::ui::IPreviewHistogram&>(histogram).widget());
    ASSERT_NE(widget, nullptr);
    const std::shared_ptr<const an::ui::HistogramDisplayData> shown = widget->getHistogram();
    ASSERT_NE(shown, nullptr);
    ASSERT_EQ(shown->channels.size(), expected.channels.size());
    for (std::size_t channel = 0u; channel < expected.channels.size(); ++channel)
    {
        EXPECT_EQ(shown->channels[channel].bins, expected.channels[channel].bins) << "channel " << channel;
        EXPECT_EQ(shown->channels[channel].clippedLow, expected.channels[channel].clippedLow) << "channel " << channel;
        EXPECT_EQ(shown->channels[channel].clippedHigh, expected.channels[channel].clippedHigh)
            << "channel " << channel;
        EXPECT_EQ(shown->channels[channel].samples, expected.channels[channel].samples) << "channel " << channel;
    }
}

/// Whether two presentations are the same bytes.
[[nodiscard]] bool samePresentation(const an::ui::PresentedImage& left, const an::ui::PresentedImage& right)
{
    if (left.width() != right.width() || left.height() != right.height() || left.format() != right.format())
    {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(left.width()) * left.bytesPerPixel();
    for (std::uint32_t y = 0u; y < left.height(); ++y)
    {
        const std::uint8_t* leftRow = left.data() + static_cast<std::size_t>(y) * left.layout().rowStrideBytes();
        const std::uint8_t* rightRow = right.data() + static_cast<std::size_t>(y) * right.layout().rowStrideBytes();
        if (std::memcmp(leftRow, rightRow, rowBytes) != 0)
        {
            return false;
        }
    }
    return true;
}

/// Turns the event loop until the window has shown one more interactive frame than it had.
[[nodiscard]] bool interactiveFrameShown(const SliderProcess& process, const ProcessRunTelemetry& before)
{
    return pumpUntil([&process, &before]() {
        return process.runTelemetry().interactivePreviewsCommitted > before.interactivePreviewsCommitted;
    });
}

/// The results of the interactive frames among a process's preview results.
[[nodiscard]] std::vector<SliderResult> interactiveResults(const SliderProcess& process)
{
    std::vector<SliderResult> found;
    for (const SliderResult& result : process.previewResults())
    {
        if (result.frameKind == an::ui::PreviewFrameKind::Interactive)
        {
            found.push_back(result);
        }
    }
    return found;
}

/// Opens a slider process of some options over a target, turns its preview on and zooms its preview view to a scale.
[[nodiscard]] SliderHandle openZoomed(ImageViewControl& target, const SliderProcessOptions& options, double scale)
{
    SliderHandle process = openSliderProcess(target, options);
    process->previewCheckbox->setChecked(true);
    EXPECT_TRUE(zoomPreview(*process, scale));
    return process;
}

} // namespace

// A frame is one admission: it processes, presents what the preview view paints and counts what the dialog's histogram
// shows, and neither the view nor the histogram does any of it again - the view paints the frame's bytes as they came,
// which are the bytes its own presentation would have made, and the histogram shows the frame's count of its result.
TEST(PreviewFrameOperation, AFrameIsOneAdmissionThatPresentsWhatTheViewPaintsAndCountsWhatTheHistogramShows)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.histogram = true}, 0.25);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    auto* histogram = dynamic_cast<an::ui::PreviewHistogramView*>(process->histogram());
    ASSERT_NE(histogram, nullptr);

    const std::uint64_t admittedBefore = session.admission().admitted;
    const ImageViewPresentationTelemetry viewBefore = widget->presentationTelemetry();
    const an::ui::HistogramCountTelemetry countsBefore = histogram->scheduler().telemetry();
    const ProcessRunTelemetry runsBefore = process->runTelemetry();
    process->spinBox->setValue(20);
    ASSERT_TRUE(settleSlider(*process));

    const ProcessRunTelemetry runs = process->runTelemetry();
    ASSERT_EQ(runs.previewsCommitted - runsBefore.previewsCommitted, 1u);
    EXPECT_EQ(session.admission().admitted - admittedBefore, 1u) << "a frame is one admission";
    const ImageViewPresentationTelemetry view = widget->presentationTelemetry();
    EXPECT_EQ(view.presented - viewBefore.presented, 0u) << "the view presented the frame's result itself";
    EXPECT_EQ(view.adopted - viewBefore.adopted, 1u);
    // The GUI thread paints the frame's bytes as they came: it makes no pixmap of them, and the paint scales them by
    // less than two - the residual between the reduction's whole factor and the zoom.
    EXPECT_EQ(view.pixmapsMade - viewBefore.pixmapsMade, 0u) << "the GUI thread made a pixmap of the frame";
    const double paintScale =
        static_cast<double>(view.presentedFactor) * widget->getCurrentScaleFactor() * widget->devicePixelRatioF();
    EXPECT_GT(paintScale, 0.5) << "the paint reduces the frame's bytes by two or more";
    EXPECT_LE(paintScale, 1.0) << "the paint magnifies the frame's bytes below full zoom";
    EXPECT_EQ(runs.presentationsAdopted - runsBefore.presentationsAdopted, 1u);
    const an::ui::HistogramCountTelemetry counts = histogram->scheduler().telemetry();
    EXPECT_EQ(counts.started - countsBefore.started, 0u) << "the histogram counted the frame's result itself";
    EXPECT_EQ(counts.handed - countsBefore.handed, 1u);
    EXPECT_EQ(runs.histogramsCounted - runsBefore.histogramsCounted, 1u);

    // What the view paints is what its own presentation of the document would be.
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(settings->proxyFactor, 4u);
    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = process->previewView()->document().read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<std::shared_ptr<const an::ui::DisplayProxy>> proxy =
        an::ui::DisplayProxy::build(scope->engine(), scope->execution(), *lease, settings->proxyFactor);
    ASSERT_TRUE(proxy.hasValue()) << proxy.status().message();
    an::Result<an::ui::ViewPresentationOutcome> expected = an::ui::presentForView(
        scope->engine(), scope->execution(), *lease, *settings, nullptr, &session.memory(), proxy->get());
    ASSERT_TRUE(expected.hasValue()) << expected.status().message();
    ASSERT_NE(widget->presentedImage(), nullptr);
    EXPECT_TRUE(samePresentation(*widget->presentedImage(), *expected->image));
    // What the histogram shows is the count of the result.
    an::Result<an::ui::HistogramDisplayData> count =
        an::ui::computeHistogramDisplayData(scope->execution(), *lease, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(count.hasValue());
    expectShown(*histogram, *count);
}

// An interactive frame of a process that declares it may runs over the target reduced by the preview view's factor -
// a source of the reduced size with a revision of its own, which a measurement of it can never be filed under the
// target's - and is shown by its presentation alone, the preview's document left as the last settled frame left it;
// the settled frame that follows the release runs over the whole target and is published there.
TEST(PreviewFrameOperation, AnInteractiveFrameOfAReducibleProcessRunsOverTheTargetReducedByTheViewsFactor)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true}, 0.25);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const an::consumer::AssetRevision documentBefore = process->previewView()->document().asset()->revision();
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    const SliderRun& interactive = previews.front();
    EXPECT_EQ(interactive.frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(interactive.proxyFactor, 4u);
    EXPECT_EQ(interactive.width, 160u);
    EXPECT_EQ(interactive.height, 120u);
    EXPECT_FALSE(interactive.partOfTarget);
    EXPECT_NE(interactive.sourceIdentity, target->document().asset()->identity())
        << "a frame over a reduction was handed the target itself";
    EXPECT_EQ(process->runTelemetry().viewFramesCommitted - before.viewFramesCommitted, 1u);
    EXPECT_EQ(process->runTelemetry().viewFrameFactor, 4u);
    EXPECT_EQ(process->previewView()->document().asset()->revision(), documentBefore)
        << "an interactive frame over a reduction was published to the preview's document";
    EXPECT_EQ(widget->presentationTelemetry().presentedFactor, 4u);

    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    const std::vector<SliderRun> all = runsFrom(process->runs(), from);
    ASSERT_GE(all.size(), 2u);
    EXPECT_EQ(all.back().frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_EQ(all.back().proxyFactor, 1u);
    EXPECT_EQ(all.back().width, 640u);
    EXPECT_EQ(all.back().sourceIdentity, target->document().asset()->identity());
    EXPECT_NE(process->previewView()->document().asset()->revision(), documentBefore);
    EXPECT_EQ(context->engineSession().admission().admitted - admittedBefore, all.size())
        << "every frame, interactive or settled, is one admission";
}

// The framework adds nothing to a frame over a reduction: its result is the body run over the target reduced, sample
// for sample.
TEST(PreviewFrameOperation, AnInteractiveFramesResultIsTheBodyRunOverTheTargetReduced)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true}, 0.25);
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderResult> results = interactiveResults(*process);
    ASSERT_FALSE(results.empty());
    const SliderResult& result = results.front();
    EXPECT_EQ(result.proxyFactor, 4u);

    std::optional<an::consumer::OperationScope> scope = admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = target->document().read();
    ASSERT_TRUE(lease.hasValue());
    an::Result<an::consumer::ImageAsset> reduced =
        an::ui::reduceForPreview(session, scope->engine(), scope->execution(), *lease, 4u);
    ASSERT_TRUE(reduced.hasValue()) << reduced.status().message();
    an::Result<an::consumer::AssetReadLease> reducedLease = reduced->read();
    ASSERT_TRUE(reducedLease.hasValue());
    const std::vector<double> reducedSamples = samplesOf(*reducedLease);
    ASSERT_EQ(result.samples.size(), reducedSamples.size());
    for (std::size_t index = 0u; index < reducedSamples.size(); ++index)
    {
        ASSERT_EQ(result.samples[index], scaled16(reducedSamples[index], 30)) << "sample " << index;
    }
    process->slider->setSliderDown(false);
}

// The settled frame that ends a drag of a process whose interactive frames ran over a reduction is the Apply, sample
// for sample.
TEST(PreviewFrameOperation, TheSettledFrameOfAReducibleProcessIsItsApply)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true}, 0.25);

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    const std::vector<double> preview = documentSamples(process->previewView()->document());

    QAbstractButton* apply = process->buttons->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    apply->setEnabled(true);
    apply->click();
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(documentSamples(target->document()), preview) << "the settled frame is not the Apply";
}

// A process that declares nothing runs every frame over the whole target, and its frames are still one operation that
// the view paints as it came.
TEST(PreviewFrameOperation, AProcessThatDeclaresNothingRunsEveryFrameOverTheWholeTarget)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{}, 0.25);
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    EXPECT_EQ(previews.front().frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews.front().proxyFactor, 1u);
    EXPECT_EQ(previews.front().width, 640u);
    EXPECT_EQ(previews.front().sourceIdentity, target->document().asset()->identity());
    EXPECT_EQ(process->runTelemetry().viewFramesCommitted, before.viewFramesCommitted);
    EXPECT_GT(process->runTelemetry().presentationsAdopted, before.presentationsAdopted);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// A process that declares nothing - every stretch among them - shows in an interactive frame exactly what its settled
// frame of the same parameters shows: the samples, the statistics the view presented them through and the histogram
// the dialog shows, bit for bit, for nothing of the frame is approximated.
TEST(PreviewFrameOperation, AnInteractiveFrameOfAProcessThatDeclaresNothingShowsWhatItsSettledFrameShows)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.histogram = true});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    process->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    auto* histogram = dynamic_cast<HistogramWidget*>(process->histogram()->widget());
    ASSERT_NE(histogram, nullptr);
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<double> interactiveSamples = documentSamples(process->previewView()->document());
    const std::optional<an::DisplayPresentationAnalysis> interactiveAnalysis = widget->presentedAnalysis();
    const std::shared_ptr<const an::ui::HistogramDisplayData> interactiveCounts = histogram->getHistogram();
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_GT(process->runTelemetry().settledPreviewsCommitted, before.settledPreviewsCommitted);

    EXPECT_EQ(documentSamples(process->previewView()->document()), interactiveSamples);
    const std::optional<an::DisplayPresentationAnalysis> settledAnalysis = widget->presentedAnalysis();
    ASSERT_TRUE(interactiveAnalysis.has_value() && settledAnalysis.has_value());
    EXPECT_EQ(settledAnalysis->medians, interactiveAnalysis->medians);
    EXPECT_EQ(settledAnalysis->medianAbsoluteDeviations, interactiveAnalysis->medianAbsoluteDeviations);
    ASSERT_NE(interactiveCounts, nullptr);
    ASSERT_NE(histogram->getHistogram(), nullptr);
    ASSERT_EQ(histogram->getHistogram()->channels.size(), interactiveCounts->channels.size());
    for (std::size_t channel = 0u; channel < interactiveCounts->channels.size(); ++channel)
    {
        EXPECT_EQ(histogram->getHistogram()->channels[channel].bins, interactiveCounts->channels[channel].bins);
    }
}

// At full scale and above, where the view shows a part of the target, an interactive frame of a reducible process runs
// over the part the view shows - a part of the target the framework cut for the frame - and the view paints it as a
// layer over the part it covers, its own presentation of the region around it left as it was.
TEST(PreviewFrameOperation, AtFullScaleAnInteractiveFrameRunsOverThePartTheViewShows)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true}, 4.0);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 1u);
    ASSERT_TRUE(settings->region.has_value()) << "at four times the view shows a part of the target";
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part(static_cast<int>(shown->x), static_cast<int>(shown->y), static_cast<int>(shown->width),
                     static_cast<int>(shown->height));
    const QRect presented = widget->presentedRegion();
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    const SliderRun& interactive = previews.front();
    EXPECT_EQ(interactive.frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(interactive.proxyFactor, 1u);
    EXPECT_TRUE(interactive.partOfTarget);
    EXPECT_EQ(interactive.width, static_cast<std::uint32_t>(part.width()));
    EXPECT_EQ(interactive.height, static_cast<std::uint32_t>(part.height()));
    EXPECT_EQ(process->runTelemetry().viewFramesCommitted - before.viewFramesCommitted, 1u);
    EXPECT_EQ(process->runTelemetry().viewFramesLayered - before.viewFramesLayered, 1u);
    EXPECT_EQ(widget->presentationLayerRegion(), part);
    EXPECT_EQ(widget->presentedRegion(), presented) << "the view's own presentation was replaced";
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// A process whose body reads a pixel's neighbours states how far: at full scale its interactive frame reads the part
// the view shows grown by that reach, within the target, and shows the part alone, so the part's edge is answered as it
// is over the whole image.
TEST(PreviewFrameOperation, AtFullScaleAnInteractiveFrameReadsThePartGrownByTheProcesssReachAndShowsThePart)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    constexpr std::uint32_t kReach = 12u;
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true, .regionMargin = kReach}, 4.0);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 1u);
    ASSERT_TRUE(settings->region.has_value()) << "at four times the view shows a part of the target";
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part(static_cast<int>(shown->x), static_cast<int>(shown->y), static_cast<int>(shown->width),
                     static_cast<int>(shown->height));
    const int reach = static_cast<int>(kReach);
    const QRect grown = part.adjusted(-reach, -reach, reach, reach).intersected(QRect(0, 0, 640, 480));
    ASSERT_NE(grown, part) << "the reach grows the part within the target";
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    const SliderRun& interactive = previews.front();
    EXPECT_EQ(interactive.frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(interactive.proxyFactor, 1u);
    EXPECT_TRUE(interactive.partOfTarget);
    EXPECT_EQ(interactive.width, static_cast<std::uint32_t>(grown.width())) << "the body reads the part grown";
    EXPECT_EQ(interactive.height, static_cast<std::uint32_t>(grown.height()));
    EXPECT_EQ(widget->presentationLayerRegion(), part) << "the frame shows the part alone";
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// The Gaussian blur reads a kernel's reach around every pixel and states it: an interactive frame at full scale answers
// the edge of the part the view shows as its settled frame answers it over the whole image, so the bytes the view
// paints of the part are the same in both.
TEST(PreviewFrameOperation, AGaussianBlursInteractiveFrameAtFullScalePaintsWhatItsSettledFrameDoes)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    // Stars on a rippled sky, so every edge of the region the view shows has structure a blur spreads across it.
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 640u, 480u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            double value = 0.25 + 0.1 * std::sin(static_cast<double>(x) / 6.0) * std::cos(static_cast<double>(y) / 5.0);
            for (std::uint32_t star = 0u; star < 40u; ++star)
            {
                const double dx = static_cast<double>(x) - static_cast<double>((star * 97u) % 640u);
                const double dy = static_cast<double>(y) - static_cast<double>((star * 61u) % 480u);
                value += 0.6 * std::exp(-(dx * dx + dy * dy) / 8.0);
            }
            return static_cast<float>((std::min)(1.0, value));
        });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("stars", std::move(*document));
    ASSERT_NE(target, nullptr);
    dialogs::DialogHandle dialog = dialogs::openDialog<GaussianBlurProcess>(*target);
    auto* sigma = dialogs::child<QDoubleSpinBox>(*dialog, "sigmaValue");
    auto* slider = dialogs::child<QSlider>(*dialog, "sigmaSlider");
    auto* preview = dialogs::child<QCheckBox>(*dialog, "preview");
    ASSERT_TRUE(sigma != nullptr && slider != nullptr && preview != nullptr);
    sigma->setValue(3.0);
    dialog->show();
    preview->setChecked(true);
    ASSERT_TRUE(dialogs::settleDialog(*dialog));
    ASSERT_NE(dialog->previewView(), nullptr);
    ImageViewWidget* widget = dialog->previewView()->getViewWidget();
    ASSERT_NE(widget, nullptr);
    widget->scaleImageView(4.0, true, true);
    ASSERT_TRUE(dialogs::settleDialog(*dialog));
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 1u);
    ASSERT_TRUE(settings->region.has_value()) << "at four times the view shows a part of the target";

    const ProcessRunTelemetry before = dialog->runTelemetry();
    slider->setSliderDown(true);
    sigma->setValue(3.5);
    ASSERT_TRUE(pumpUntil(
        [&dialog, &before]() { return dialog->runTelemetry().viewFramesCommitted > before.viewFramesCommitted; }));
    const std::shared_ptr<const an::ui::PresentedImage> interactive = widget->presentationLayer();
    const QRect interactiveRegion = widget->presentationLayerRegion();
    ASSERT_NE(interactive, nullptr) << "the frame over the part shown is painted as a layer";
    slider->setSliderDown(false);
    ASSERT_TRUE(dialogs::settleDialog(*dialog));
    EXPECT_GT(dialog->runTelemetry().previewsCommitted, before.previewsCommitted) << "the release settled the frame";
    const std::shared_ptr<const an::ui::PresentedImage> settled = widget->presentedImage();
    ASSERT_NE(settled, nullptr);
    const QRect settledRegion = widget->presentedRegion();
    ASSERT_TRUE(settledRegion.contains(interactiveRegion));
    ASSERT_EQ(settled->format(), interactive->format());
    const std::uint32_t pixelBytes = interactive->layout().elementSizeBytes() * interactive->layout().samplesPerPixel();
    const auto left = static_cast<std::uint32_t>(interactiveRegion.x() - settledRegion.x());
    const auto top = static_cast<std::uint32_t>(interactiveRegion.y() - settledRegion.y());
    int largest = 0;
    for (std::uint32_t y = 0u; y < interactive->height(); ++y)
    {
        const std::uint8_t* one = interactive->data() + y * interactive->layout().rowStrideBytes();
        const std::uint8_t* other =
            settled->data() + (top + y) * settled->layout().rowStrideBytes() + left * pixelBytes;
        for (std::uint32_t i = 0u; i < interactive->width() * pixelBytes; ++i)
        {
            largest = (std::max)(largest, std::abs(static_cast<int>(one[i]) - static_cast<int>(other[i])));
        }
    }
    EXPECT_EQ(largest, 0) << "the interactive frame paints the part as the settled frame does";
}

// A change made without a held slider - a spin box typed into - is answered at once by an interactive frame where
// one costs what the view paints, and settled after the settle delay.
TEST(PreviewFrameOperation, AChangeMadeWithNoSliderHeldIsAnsweredByAnInteractiveFrameThenSettled)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true}, 0.25);
    const std::size_t from = process->runs().size();

    process->spinBox->setValue(25);
    ASSERT_TRUE(settleSlider(*process));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_EQ(previews.size(), 2u);
    EXPECT_EQ(previews[0].frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews[0].proxyFactor, 4u);
    EXPECT_EQ(previews[1].frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_EQ(previews[1].proxyFactor, 1u);
    EXPECT_EQ(previews[1].value, 25);
}

// An interactive frame of a body that maps each channel alone, over a sixteen-bit target, shows in the dialog's
// histogram the target's codes remapped through what the body makes of every code: exactly what a count of the whole
// result shows, though the frame ran over a reduction - and nothing of the reduction was counted.
TEST(PreviewFrameHistogram, AFrameOfAMapOfEachChannelAloneShowsTheTargetsCodesRemappedAsItsWholeResultCounts)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openZoomed(*target, SliderProcessOptions{.decimated = true, .channelsAlone = true, .histogram = true}, 0.25);
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    EXPECT_EQ(process->runTelemetry().histogramsRemapped - before.histogramsRemapped, 1u);
    std::optional<an::consumer::ImageAsset> whole = scaledTarget(session, target->document(), 30);
    ASSERT_TRUE(whole.has_value());
    const std::optional<an::ui::HistogramDisplayData> expected =
        countOf(session, *whole, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(expected.has_value());
    expectShown(*process->histogram(), *expected);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().histogramsCounted - before.histogramsCounted, 1u)
        << "the settled frame counts its whole result";
    expectShown(*process->histogram(), *expected);
}

// An interactive frame of a body that mixes channels shows the count of the body run over the target sampled one pixel
// per block of the factor the frame ran at - as many samples as the reduction it ran over, never a count of that
// reduction: the whole result sampled the same way, since the body maps each pixel where it lies.
TEST(PreviewFrameHistogram, AFrameOfABodyThatMixesChannelsIsCountedOverTheTargetSampledOnePixelPerBlock)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(*target, SliderProcessOptions{.decimated = true, .histogram = true}, 0.25);
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    EXPECT_EQ(process->runTelemetry().histogramsSampled - before.histogramsSampled, 1u);
    EXPECT_EQ(process->runTelemetry().histogramsRemapped, before.histogramsRemapped);
    const std::uint32_t factor = process->runTelemetry().viewFrameFactor;
    ASSERT_GE(factor, 2u) << "the frame ran over no reduction";
    std::optional<an::consumer::ImageAsset> whole = scaledTarget(session, target->document(), 30);
    ASSERT_TRUE(whole.has_value());
    std::optional<an::consumer::ImageAsset> sampled;
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = whole->read();
        ASSERT_TRUE(lease.hasValue());
        an::Result<an::consumer::ImageAsset> made =
            an::ui::sampleForPreview(session, scope->engine(), scope->execution(), *lease, factor);
        ASSERT_TRUE(made.hasValue()) << made.status().message();
        sampled.emplace(std::move(*made));
    }
    const std::optional<an::ui::HistogramDisplayData> expected =
        countOf(session, *sampled, an::ui::HistogramResolution::Bits8);
    ASSERT_TRUE(expected.has_value());
    expectShown(*process->histogram(), *expected);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
}

// The preview of a reducible process in autostretch is stretched through the target's own analysis - measured once on
// the whole target, not on the reduction an interactive frame runs over and not on any result - in every frame of a
// drag, interactive and settled, so the stretch never moves under the adjustment.
TEST(FrozenStretch, AReducibleProcessesPreviewIsStretchedThroughTheTargetsOwnAnalysisInEveryFrame)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.decimated = true});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    const ProcessRunTelemetry beforeStretch = process->runTelemetry();
    process->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_GT(process->runTelemetry().settledPreviewsCommitted, beforeStretch.settledPreviewsCommitted)
        << "entering autostretch asked for no frame";
    ImageViewWidget* widget = process->previewView()->getViewWidget();

    // The target's own analysis, measured here over the whole of it, the admission let go before the drag.
    std::optional<an::DisplayPresentationAnalysis> analysis;
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue());
        an::ui::ViewPresentationSettings settings;
        settings.request = widget->presentationRequest();
        an::Result<an::DisplayPresentationAnalysis> measured =
            an::ui::analyzeForView(scope->engine(), scope->execution(), *lease, settings);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        analysis = *measured;
    }
    const auto expectTargets = [&analysis](const std::optional<an::DisplayPresentationAnalysis>& presented,
                                           const char* frame) {
        ASSERT_TRUE(presented.has_value()) << frame;
        EXPECT_TRUE(presented->measured) << frame;
        EXPECT_EQ(presented->medians, analysis->medians) << frame;
        EXPECT_EQ(presented->medianAbsoluteDeviations, analysis->medianAbsoluteDeviations) << frame;
        EXPECT_EQ(presented->measuredSamples, analysis->measuredSamples) << frame;
    };
    expectTargets(widget->presentedAnalysis(), "the frame that entered autostretch");
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    expectTargets(widget->presentedAnalysis(), "the view's own presentation at another zoom");

    const ProcessRunTelemetry before = process->runTelemetry();
    process->slider->setSliderDown(true);
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    expectTargets(widget->presentedAnalysis(), "the interactive frame");
    process->slider->setValue(80);
    ASSERT_TRUE(pumpUntil([&process, &before]() {
        return process->runTelemetry().interactivePreviewsCommitted >= before.interactivePreviewsCommitted + 2u;
    }));
    expectTargets(widget->presentedAnalysis(), "the second interactive frame");
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    expectTargets(widget->presentedAnalysis(), "the settled frame");
    EXPECT_EQ(process->runTelemetry().analysesMeasured, 1u) << "the target's analysis was measured more than once";
}

// The target's analysis is measured for what the preview view shows: with one channel of a colour target shown in
// autostretch it is measured for that channel - the frame's presentation refuses one measured for the whole colour
// image - so every frame of a drag, interactive and settled, is stretched by it and none fails.
TEST(FrozenStretch, TheTargetsAnalysisIsMeasuredForTheChannelThePreviewViewShows)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.decimated = true});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    process->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleSlider(*process));
    process->previewView()->displayViewChannel(ImageCompositionMode::r_mode);
    ASSERT_TRUE(settleSlider(*process));
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    ASSERT_EQ(widget->displayedChannel().scope, an::ChannelScope::Red);

    // The target's own analysis for its red channel, measured here over the whole of it.
    std::optional<an::DisplayPresentationAnalysis> analysis;
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue());
        an::ui::ViewPresentationSettings settings;
        settings.request = widget->presentationRequest();
        settings.channel = widget->displayedChannel();
        an::Result<an::DisplayPresentationAnalysis> measured =
            an::ui::analyzeForView(scope->engine(), scope->execution(), *lease, settings);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        analysis = *measured;
    }
    const auto expectTargets = [&analysis](const std::optional<an::DisplayPresentationAnalysis>& presented,
                                           const char* frame) {
        ASSERT_TRUE(presented.has_value()) << frame;
        EXPECT_TRUE(presented->measured) << frame;
        EXPECT_EQ(presented->medians, analysis->medians) << frame;
        EXPECT_EQ(presented->medianAbsoluteDeviations, analysis->medianAbsoluteDeviations) << frame;
        EXPECT_EQ(presented->measuredSamples, analysis->measuredSamples) << frame;
    };

    const ProcessRunTelemetry before = process->runTelemetry();
    process->slider->setSliderDown(true);
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    expectTargets(widget->presentedAnalysis(), "the interactive frame");
    process->slider->setValue(80);
    ASSERT_TRUE(pumpUntil([&process, &before]() {
        return process->runTelemetry().interactivePreviewsCommitted >= before.interactivePreviewsCommitted + 2u;
    }));
    expectTargets(widget->presentedAnalysis(), "the second interactive frame");
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    expectTargets(widget->presentedAnalysis(), "the settled frame");
    EXPECT_EQ(process->runTelemetry().previewsFailed, before.previewsFailed) << "a frame of the drag failed";
}

// A preview view that shows the target itself in autostretch - at the process's identity, before any frame has landed -
// measured the target's analysis presenting it: the frames take that measurement over, so the first frame of a drag
// measures nothing and costs what the others cost.
TEST(FrozenStretch, TheFramesTakeOverTheAnalysisThePreviewViewMeasuredOfTheTarget)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = gradientTarget<std::uint16_t>(session, Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.identityAtZero = true, .decimated = true});
    process->previewCheckbox->setChecked(true);
    process->show();
    ASSERT_TRUE(settleSlider(*process));
    process->previewView()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_TRUE(process->previewView()->document().isShared()) << "the preview view does not show the target itself";
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    ASSERT_TRUE(widget->presentedAnalysis().has_value()) << "the preview view did not stretch the target";
    EXPECT_EQ(process->runTelemetry().analysesMeasured, 0u);

    std::optional<an::DisplayPresentationAnalysis> analysis;
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session);
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = target->document().read();
        ASSERT_TRUE(lease.hasValue());
        an::ui::ViewPresentationSettings settings;
        settings.request = widget->presentationRequest();
        an::Result<an::DisplayPresentationAnalysis> measured =
            an::ui::analyzeForView(scope->engine(), scope->execution(), *lease, settings);
        ASSERT_TRUE(measured.hasValue()) << measured.status().message();
        analysis = *measured;
    }
    const auto expectTargets = [&analysis](const std::optional<an::DisplayPresentationAnalysis>& presented,
                                           const char* frame) {
        ASSERT_TRUE(presented.has_value()) << frame;
        EXPECT_TRUE(presented->measured) << frame;
        EXPECT_EQ(presented->medians, analysis->medians) << frame;
        EXPECT_EQ(presented->medianAbsoluteDeviations, analysis->medianAbsoluteDeviations) << frame;
        EXPECT_EQ(presented->measuredSamples, analysis->measuredSamples) << frame;
    };
    expectTargets(widget->presentedAnalysis(), "the preview view's own presentation of the target");

    const ProcessRunTelemetry before = process->runTelemetry();
    process->slider->setSliderDown(true);
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    expectTargets(widget->presentedAnalysis(), "the first interactive frame");
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    expectTargets(widget->presentedAnalysis(), "the settled frame");
    EXPECT_EQ(process->runTelemetry().analysesMeasured, 0u) << "a frame measured what the preview view had measured";
}
