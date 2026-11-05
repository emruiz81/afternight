// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A preview's frame over the part of the target the preview view shows, against the Controls library. A process whose
/// body reads at full scale and no farther than a reach it states (`PreviewScaling::NativeRegion`) runs an interactive
/// frame over the part the view shows - at every zoom, presented at the view's reduction - grown by that reach within
/// the target, and the view paints the part as a layer over what it paints: there it is the whole result's pixels. A
/// view that shows the whole target runs every frame of such a process over all of it, settled. A frame over a part
/// makes no histogram, which no part stands for, and a drag that moves again stops the settled frame still on its way.

#include "Test_ImageRTPreviewControl/ProcessDialogSupport.h"
#include "Test_ImageRTPreviewControl/SliderProcessSupport.h"
#include "controls/ImageViewScrollArea.h"

#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <QScrollBar>
#include <QSlider>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::frames;

/// Every sample of what a document holds now, every channel's in row order, as doubles.
[[nodiscard]] std::vector<double> documentSamples(const an::ui::ImageDocument& document)
{
    std::vector<double> samples;
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return samples;
    }
    lease->visit([&samples](const auto& typed) {
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

/// The rectangle a view region states.
[[nodiscard]] QRect rectOf(const an::ui::ViewRegion& region)
{
    return QRect(static_cast<int>(region.x), static_cast<int>(region.y), static_cast<int>(region.width),
                 static_cast<int>(region.height));
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

/// Turns the event loop until the window has shown one more interactive frame than it had.
[[nodiscard]] bool interactiveFrameShown(const SliderProcess& process, const ProcessRunTelemetry& before)
{
    return pumpUntil([&process, &before]() {
        return process.runTelemetry().interactivePreviewsCommitted > before.interactivePreviewsCommitted;
    });
}

/// Opens a slider process of some options over a target, turns its preview on and zooms its preview view to a scale.
[[nodiscard]] SliderHandle openZoomed(ImageViewControl& target, const SliderProcessOptions& options, double scale)
{
    SliderHandle process = openSliderProcess(target, options);
    process->previewCheckbox->setChecked(true);
    EXPECT_TRUE(zoomPreview(*process, scale));
    return process;
}

/// A sixteen-bit grey target view of a ramp broken every 61 columns and every 37 rows, so a window near any edge of a
/// part of it differs from one cut at that edge; null when it cannot be made.
[[nodiscard]] ViewHandle brokenRampTarget(const an::consumer::EngineSession& session, std::uint32_t width,
                                          std::uint32_t height)
{
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        session, width, height, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(((x % 61u) * 700u + (y % 37u) * 900u + (x / 61u) * 50u) % 65000u);
        });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("ramp", std::move(*document));
}

/// The samples of a result over a rectangle within it, every channel's in row order.
[[nodiscard]] std::vector<double> samplesWithin(const SliderResult& result, const QRect& within)
{
    std::vector<double> samples;
    for (std::uint32_t channel = 0u; channel < result.channels; ++channel)
    {
        for (int y = within.top(); y <= within.bottom(); ++y)
        {
            for (int x = within.left(); x <= within.right(); ++x)
            {
                samples.push_back(
                    result.samples[(static_cast<std::size_t>(channel) * result.height + static_cast<std::size_t>(y)) *
                                       result.width +
                                   static_cast<std::size_t>(x)]);
            }
        }
    }
    return samples;
}

/// The samples of a whole image's samples - every channel's in row order, `width` by `height` - over a rectangle.
[[nodiscard]] std::vector<double> samplesWithin(const std::vector<double>& whole, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t channels, const QRect& within)
{
    std::vector<double> samples;
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        for (int y = within.top(); y <= within.bottom(); ++y)
        {
            for (int x = within.left(); x <= within.right(); ++x)
            {
                samples.push_back(
                    whole[(static_cast<std::size_t>(channel) * height + static_cast<std::size_t>(y)) * width +
                          static_cast<std::size_t>(x)]);
            }
        }
    }
    return samples;
}

/// Applies a slider process to its target as the Apply button does and waits for the target to show it.
[[nodiscard]] bool applied(SliderProcess& process, ImageViewControl& target)
{
    QAbstractButton* apply = process.buttons->button(QDialogButtonBox::Apply);
    if (apply == nullptr)
    {
        return false;
    }
    apply->setEnabled(true);
    apply->click();
    return settleSlider(process) && settleView(target);
}

/// Whether a presented image holds, over a rectangle of its own pixels, the bytes another holds whole.
[[nodiscard]] bool holdsWithin(const an::ui::PresentedImage& whole, int left, int top,
                               const an::ui::PresentedImage& part)
{
    if (whole.format() != part.format() || left < 0 || top < 0 ||
        static_cast<std::uint32_t>(left) + part.width() > whole.width() ||
        static_cast<std::uint32_t>(top) + part.height() > whole.height())
    {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(part.width()) * part.bytesPerPixel();
    for (std::uint32_t y = 0u; y < part.height(); ++y)
    {
        const std::uint8_t* partRow = part.data() + static_cast<std::size_t>(y) * part.layout().rowStrideBytes();
        const std::uint8_t* wholeRow =
            whole.data() +
            static_cast<std::size_t>(static_cast<std::uint32_t>(top) + y) * whole.layout().rowStrideBytes() +
            static_cast<std::size_t>(left) * whole.bytesPerPixel();
        if (std::memcmp(partRow, wholeRow, rowBytes) != 0)
        {
            return false;
        }
    }
    return true;
}

} // namespace

// A body that reads a window around every pixel and states how far it reads runs, while a slider is held, over the part
// of the target the view shows at full scale, grown by that reach within the target: the part's samples are the Apply's
// there, its edges answered as the whole image answers them; the view paints the part as a layer over what it paints,
// and the preview's document stays what the last settled frame made.
TEST(PreviewFrameRegion, AnInteractiveFrameRunsOverThePartShownGrownByTheReachAndIsTheApplysAnswerThere)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    constexpr std::uint32_t kReach = 6u;
    SliderHandle process = openZoomed(
        *target, SliderProcessOptions{.nativeRegion = true, .regionMargin = kReach, .bodyReach = kReach}, 4.0);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 1u);
    ASSERT_TRUE(settings->region.has_value()) << "at four times the view shows a part of the target";
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part = rectOf(*shown);
    const int reach = static_cast<int>(kReach);
    const QRect grown = part.adjusted(-reach, -reach, reach, reach).intersected(QRect(0, 0, 640, 480));
    ASSERT_NE(grown, part) << "the reach grows the part within the target";
    const an::consumer::AssetRevision documentBefore = process->previewView()->document().asset()->revision();
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
    EXPECT_EQ(process->runTelemetry().viewFramesLayered - before.viewFramesLayered, 1u);
    EXPECT_EQ(process->previewView()->document().asset()->revision(), documentBefore)
        << "an interactive frame over a part was published to the preview's document";
    const std::vector<SliderResult> results = interactiveResults(*process);
    ASSERT_FALSE(results.empty());
    const SliderResult& result = results.front();
    ASSERT_EQ(result.width, static_cast<std::uint32_t>(grown.width()));
    ASSERT_EQ(result.height, static_cast<std::uint32_t>(grown.height()));
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    process->slider->setValue(30);
    ASSERT_TRUE(applied(*process, *target));
    const std::vector<double> apply = documentSamples(target->document());
    EXPECT_EQ(samplesWithin(result, part.translated(-grown.topLeft())), samplesWithin(apply, 640u, 480u, 3u, part))
        << "the part is not the Apply's answer there";
}

// The reach is the one the process states: stated short of what the body reads, the part's edges inside the target are
// answered as the part's own edges are, not as the whole image answers them.
TEST(PreviewFrameRegion, AReachStatedShortOfTheBodysLeavesThePartsEdgesOtherThanTheApplys)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openZoomed(*target, SliderProcessOptions{.nativeRegion = true, .regionMargin = 1u, .bodyReach = 6u}, 4.0);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    const QRect part = rectOf(*shown);
    const QRect grown = part.adjusted(-1, -1, 1, 1).intersected(QRect(0, 0, 640, 480));
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderResult> results = interactiveResults(*process);
    ASSERT_FALSE(results.empty());
    ASSERT_EQ(results.front().width, static_cast<std::uint32_t>(grown.width()));
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    ASSERT_TRUE(applied(*process, *target));
    const std::vector<double> apply = documentSamples(target->document());
    EXPECT_NE(samplesWithin(results.front(), part.translated(-grown.topLeft())),
              samplesWithin(apply, 640u, 480u, 3u, part));
}

// Zoomed out over a part of the target, the frame runs at full scale over the part the view shows, aligned to the
// blocks of the view's reduction and grown by the reach, and is presented at that reduction: the layer the view paints
// is, byte for byte, what the settled frame then paints there. At four tenths the part shown does not fall on the
// blocks of the reduction by two, so a part left unaligned would be reduced in other blocks than the whole image.
TEST(PreviewFrameRegion, AtAReductionTheFrameRunsAtFullScaleOverThePartShownAndIsPresentedReduced)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = brokenRampTarget(context->engineSession(), 2400u, 1600u);
    ASSERT_NE(target, nullptr);
    constexpr std::uint32_t kReach = 5u;
    SliderHandle process = openZoomed(
        *target, SliderProcessOptions{.nativeRegion = true, .regionMargin = kReach, .bodyReach = kReach}, 0.4);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 2u);
    ASSERT_TRUE(settings->region.has_value()) << "at four tenths the view shows a part of the target";
    const auto onTheBlocks = [](const an::ui::ViewRegion& region) {
        return QRect(QPoint(static_cast<int>(region.x / 2u * 2u), static_cast<int>(region.y / 2u * 2u)),
                     QPoint(static_cast<int>((region.x + region.width + 1u) / 2u * 2u) - 1,
                            static_cast<int>((region.y + region.height + 1u) / 2u * 2u) - 1))
            .intersected(QRect(0, 0, 2400, 1600));
    };
    std::optional<an::ui::ViewRegion> shown = widget->shownImageRegion();
    ASSERT_TRUE(shown.has_value());
    // Whether the part a viewport shows falls on the blocks depends on the viewport's size, which the platform's window
    // decides; the view is moved a device pixel at a time until it does not.
    const ImageViewScrollArea* area = process->previewView()->getScrollArea();
    ASSERT_NE(area, nullptr);
    for (int moved = 0; moved < 8 && onTheBlocks(*shown) == rectOf(*shown); ++moved)
    {
        area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() + 1);
        area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + 1);
        ASSERT_TRUE(settleSlider(*process));
        shown = widget->shownImageRegion();
        ASSERT_TRUE(shown.has_value());
    }
    const QRect part = onTheBlocks(*shown);
    ASSERT_NE(part, rectOf(*shown)) << "the part shown falls on the blocks of the reduction";
    const int reach = static_cast<int>(kReach);
    const QRect grown = part.adjusted(-reach, -reach, reach, reach).intersected(QRect(0, 0, 2400, 1600));
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(40);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    EXPECT_EQ(previews.front().frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(previews.front().proxyFactor, 1u) << "the frame runs at full scale";
    EXPECT_EQ(previews.front().width, static_cast<std::uint32_t>(grown.width()));
    EXPECT_EQ(previews.front().height, static_cast<std::uint32_t>(grown.height()));
    EXPECT_EQ(widget->presentationLayerRegion(), part);
    const std::shared_ptr<const an::ui::PresentedImage> layer = widget->presentationLayer();
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->width(), static_cast<std::uint32_t>(part.width()) / 2u) << "the part is presented reduced";
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    EXPECT_EQ(widget->presentationLayer(), nullptr) << "the settled frame's presentation replaced the layer";
    const std::shared_ptr<const an::ui::PresentedImage> settled = widget->presentedImage();
    ASSERT_NE(settled, nullptr);
    const QRect settledRegion = widget->presentedRegion();
    ASSERT_TRUE(settledRegion.contains(part));
    EXPECT_TRUE(holdsWithin(*settled, (part.x() - settledRegion.x()) / 2, (part.y() - settledRegion.y()) / 2, *layer))
        << "the layer is not what the settled frame paints over the part";
}

// Where the view shows the whole target, a frame of a body that reads at full scale has no part to run over: every
// frame of a drag runs over the whole target as a settled frame, which may take every thread a preview can, and the
// drag owes no other frame for it.
TEST(PreviewFrameRegion, WhereTheViewShowsTheWholeTargetEveryFrameIsSettledOverAllOfIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openZoomed(*target, SliderProcessOptions{.nativeRegion = true, .regionMargin = 4u, .bodyReach = 4u}, 0.25);
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_FALSE(settings->region.has_value()) << "fitted, the view shows the whole target";
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    for (int value = 10; value <= 30; value += 10)
    {
        process->slider->setValue(value);
        ASSERT_TRUE(pumpUntil([&process, from, value]() {
            const std::vector<SliderRun> runs = runsFrom(process->runs(), from);
            return !runs.empty() && runs.back().value == value;
        }));
    }
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_EQ(after.interactivePreviewsCommitted, before.interactivePreviewsCommitted);
    EXPECT_EQ(after.viewFramesCommitted, before.viewFramesCommitted);
    EXPECT_EQ(after.previewsDiscarded, before.previewsDiscarded) << "a frame of the drag was stopped";
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_FALSE(previews.empty());
    const std::uint32_t reservation = an::consumer::previewReservationWidth(context->engineSession().width());
    for (const SliderRun& run : previews)
    {
        EXPECT_EQ(run.frameKind, an::ui::PreviewFrameKind::Settled);
        EXPECT_FALSE(run.partOfTarget);
        EXPECT_EQ(run.width, 640u);
        EXPECT_GT(run.granted, reservation) << "a settled frame took no more than the preview reservation";
    }
    EXPECT_EQ(previews.back().value, 30);
}

// A frame whose reach around the part shown takes in the whole target has no part to run over either: it runs over
// the whole target as a settled frame, and the drag's next change waits for it rather than stopping it, so a drag of
// such frames shows every one of them.
TEST(PreviewFrameRegion, AFrameWhoseReachTakesInTheWholeTargetIsSettledAndTheDragDoesNotStopIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openZoomed(*target, SliderProcessOptions{.nativeRegion = true, .regionMargin = 100000u, .bodyReach = 2u}, 4.0);
    const std::optional<an::ui::ViewPresentationSettings> settings =
        process->previewView()->getViewWidget()->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_TRUE(settings->region.has_value()) << "at four times the view shows a part of the target";
    const std::size_t from = process->runs().size();
    const ProcessRunTelemetry before = process->runTelemetry();

    // The first frame of the drag waits at the gate while the drag moves on.
    process->gate.close();
    const int arrivedBefore = process->gate.arrived();
    process->slider->setSliderDown(true);
    process->slider->setValue(10);
    ASSERT_TRUE(pumpUntil([&process, arrivedBefore]() { return process->gate.arrived() > arrivedBefore; }));
    process->slider->setValue(20);
    pumpQuietly();
    process->gate.open();
    ASSERT_TRUE(pumpUntil([&process, from]() {
        const std::vector<SliderRun> runs = runsFrom(process->runs(), from);
        return !runs.empty() && runs.back().value == 20;
    }));
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));

    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_EQ(after.previewsDiscarded, before.previewsDiscarded) << "the drag stopped a frame of its own";
    EXPECT_EQ(after.interactivePreviewsCommitted, before.interactivePreviewsCommitted);
    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_GE(previews.size(), 2u);
    EXPECT_EQ(previews.front().value, 10);
    for (const SliderRun& run : previews)
    {
        EXPECT_EQ(run.frameKind, an::ui::PreviewFrameKind::Settled);
        EXPECT_FALSE(run.partOfTarget);
        EXPECT_EQ(run.width, 640u);
    }
}

// A frame over a part of the target makes no histogram: no count of a part or of a sample stands for the whole result
// of a body that reads at full scale, so the dialog's histogram keeps what the last settled frame counted until the
// settled frame that follows the drag counts the whole result.
TEST(PreviewFrameRegion, AFrameOverAPartMakesNoHistogramAndTheSettledFrameCountsTheWholeResult)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openZoomed(
        *target, SliderProcessOptions{.nativeRegion = true, .regionMargin = 3u, .bodyReach = 3u, .histogram = true},
        4.0);
    auto* histogram = dynamic_cast<HistogramWidget*>(process->histogram()->widget());
    ASSERT_NE(histogram, nullptr);
    const std::shared_ptr<const an::ui::HistogramDisplayData> shownBefore = histogram->getHistogram();
    const ProcessRunTelemetry before = process->runTelemetry();

    process->slider->setSliderDown(true);
    process->slider->setValue(30);
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const ProcessRunTelemetry duringDrag = process->runTelemetry();
    EXPECT_EQ(duringDrag.viewFramesLayered - before.viewFramesLayered, 1u);
    EXPECT_EQ(duringDrag.histogramsCounted, before.histogramsCounted);
    EXPECT_EQ(duringDrag.histogramsRemapped, before.histogramsRemapped);
    EXPECT_EQ(duringDrag.histogramsSampled, before.histogramsSampled);
    EXPECT_EQ(histogram->getHistogram(), shownBefore) << "a frame over a part changed the dialog's histogram";

    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().histogramsCounted - duringDrag.histogramsCounted, 1u)
        << "the settled frame counts its whole result";
}

// A drag that moves again while the settled frame it was owed is still on its way stops that frame: it shows
// parameters the drag has left behind, over the whole target, and the drag's frame over what the view shows starts
// at once rather than after it. The stopped frame shows nothing.
TEST(PreviewFrameRegion, ADragThatMovesAgainStopsTheSettledFrameOnItsWay)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process =
        openZoomed(*target, SliderProcessOptions{.nativeRegion = true, .regionMargin = 2u, .bodyReach = 2u}, 4.0);
    ProcessRunTelemetry before = process->runTelemetry();
    process->slider->setSliderDown(true);
    process->slider->setValue(10);
    ASSERT_TRUE(interactiveFrameShown(*process, before));

    // Held still, the drag is settled after the settle delay: that frame waits at the gate, over the whole target and
    // still on its way.
    process->gate.close();
    const int arrivedBefore = process->gate.arrived();
    const std::size_t from = process->runs().size();
    before = process->runTelemetry();
    ASSERT_TRUE(pumpUntil([&process, arrivedBefore]() { return process->gate.arrived() > arrivedBefore; }));
    std::vector<SliderRun> runs = runsFrom(process->runs(), from);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs.front().frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_EQ(runs.front().width, 640u);

    // The drag moves again: the settled frame is stopped, and the drag's frame runs next.
    process->slider->setValue(20);
    ASSERT_TRUE(pumpUntil([&process, from]() { return runsFrom(process->runs(), from).size() >= 2u; }));
    process->gate.open();
    ASSERT_TRUE(interactiveFrameShown(*process, before));
    const ProcessRunTelemetry after = process->runTelemetry();
    EXPECT_EQ(after.previewsDiscarded - before.previewsDiscarded, 1u) << "the settled frame was not stopped";
    EXPECT_EQ(after.settledPreviewsCommitted, before.settledPreviewsCommitted);
    runs = runsFrom(process->runs(), from);
    ASSERT_GE(runs.size(), 2u);
    EXPECT_EQ(runs[1].frameKind, an::ui::PreviewFrameKind::Interactive);
    EXPECT_EQ(runs[1].value, 20);
    EXPECT_TRUE(runs[1].partOfTarget);
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(runsFrom(process->runs(), from).back().frameKind, an::ui::PreviewFrameKind::Settled);
}
