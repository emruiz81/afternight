// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a preview does around its frames, against the Controls library: the preview view shows the target's own image,
/// shared and never copied, until its first frame lands and whenever the parameters change nothing, which runs no
/// body; a result a frame kept - its own, or one its body made beside it - shows for parameters of its key with no
/// frame, blended as a frame's is, and nothing kept of an image before it changed is shown; an Apply and a Reset are
/// each followed by one preview, of the parameters the reset leaves; a failed frame is reported in the window's banner
/// once for each cause and never in a box, and the banner is cleared once a frame is shown; and the waiting spinner is
/// shown only for a frame that runs past its delay.

#include "Test_ImageRTPreviewControl/SliderProcessSupport.h"

#include <QAbstractButton>
#include <QEventLoop>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::frames;
using namespace std::chrono_literals;

/// Turns the event loop for a while, whatever happens.
void pumpFor(std::chrono::milliseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
}

/// Whether the preview view shows the target's own image, shared rather than copied.
[[nodiscard]] bool showsTheTargetItself(const SliderProcess& process, const ImageViewControl& target)
{
    const ImageViewControl* preview = process.previewView();
    return preview != nullptr && preview->document().isShared() &&
           preview->document().asset() == target.document().asset();
}

/// The preview view's sample at a pixel.
[[nodiscard]] double previewSample(const SliderProcess& process, std::uint32_t x, std::uint32_t y)
{
    return process.previewView() != nullptr ? documentSample(process.previewView()->document(), x, y)
                                            : std::numeric_limits<double>::quiet_NaN();
}

/// Every sample of the grey 8 by 6 image the preview view shows, in row order.
[[nodiscard]] std::vector<double> previewSamples(const SliderProcess& process)
{
    std::vector<double> samples;
    for (std::uint32_t y = 0u; y < 6u; ++y)
    {
        for (std::uint32_t x = 0u; x < 8u; ++x)
        {
            samples.push_back(previewSample(process, x, y));
        }
    }
    return samples;
}

/// A grey 8 by 6 Float32 mask view, from no weight at the left edge to all of it at the right; null when it cannot be
/// made.
[[nodiscard]] ViewHandle leftToRightMask(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [](std::uint32_t x, std::uint32_t) { return static_cast<float>(x) / 7.0f; });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("mask", std::move(*document));
}

} // namespace

// Turning the preview on shows the target's own image until the first frame lands, with no copy of it made; the frame
// then shows its result in a document of the preview's own.
TEST(PreviewLifecycle, ThePreviewViewSharesTheTargetUntilTheFirstFrameLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->spinBox->setValue(20);
    process->gate.close();

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    EXPECT_TRUE(showsTheTargetItself(*process, *target)) << "the preview showed a copy, or nothing, before its frame";

    process->gate.open();
    ASSERT_TRUE(settleSlider(*process));
    const an::ui::ImageDocument& shown = process->previewView()->document();
    EXPECT_FALSE(shown.isShared());
    EXPECT_NE(shown.asset(), target->document().asset());
    EXPECT_NEAR(documentSample(shown, 0u, 0u), 0.25 * 1.2, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6) << "the target was not written";
}

// Parameters that change nothing run no body: the preview view shows the target's own image, and goes back to a
// result of its own with the next change that does something.
TEST(PreviewLifecycle, AFrameOfTheIdentityRunsNothingAndShowsTheTargetsOwnImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.identityAtZero = true});

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_TRUE(process->runs().empty()) << "a frame of the identity ran its body";
    EXPECT_EQ(process->runTelemetry().identityPreviews, 1u);
    EXPECT_EQ(process->runTelemetry().previewsStarted, 0u);
    EXPECT_TRUE(showsTheTargetItself(*process, *target));

    process->spinBox->setValue(40);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_EQ(process->runs().size(), 1u);
    EXPECT_FALSE(process->previewView()->document().isShared());
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25 * 1.4, 1e-6);

    process->spinBox->setValue(0);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), 1u) << "going back to the identity ran the body";
    EXPECT_EQ(process->runTelemetry().identityPreviews, 2u);
    EXPECT_TRUE(showsTheTargetItself(*process, *target));
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25, 1e-6);
}

// A frame keeps its result and those its body made beside it, each under the key of its parameters: parameters whose
// result was kept show it at once, shared, with no frame; others run one, which keeps what it made in place of what was
// kept before.
TEST(PreviewLifecycle, ParametersWhoseResultAFrameKeptShowItWithNoFrame)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.alternatives = {20, 40}});
    process->spinBox->setValue(20);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t first = process->runs().size();
    ASSERT_GE(first, 1u);
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.2, 1e-6);
    EXPECT_EQ(process->keptPreviewCount(), 2u) << "the frame of 20 keeps its own result and that of 40";
    const ProcessRunTelemetry opened = process->runTelemetry();

    process->spinBox->setValue(40);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), first) << "the result the frame's body made beside its own was made again";
    EXPECT_EQ(process->runTelemetry().previewsStarted, opened.previewsStarted);
    EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 1u);
    EXPECT_TRUE(process->previewView()->document().isShared()) << "the kept result was copied";
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.4, 1e-6);

    process->spinBox->setValue(20);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), first) << "the frame's own result was not kept";
    EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 2u);
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.2, 1e-6);

    process->spinBox->setValue(30);
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_EQ(process->runs().size(), first + 1u) << "parameters no frame made a result for ran no frame";
    EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 2u);
    EXPECT_FALSE(process->previewView()->document().isShared());
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.3, 1e-6);
    EXPECT_EQ(process->keptPreviewCount(), 3u);

    // The frame of 30 kept its own result and the results of 20 and 40 it made beside it.
    process->spinBox->setValue(40);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), first + 1u);
    EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 3u);
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.4, 1e-6);
    process->spinBox->setValue(30);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), first + 1u);
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.25 * 1.3, 1e-6);
}

// A result kept beside a frame's own is blended through the target's mask as the frame's result is: it is what a frame
// of its parameters shows.
TEST(PreviewLifecycle, AResultKeptBesideAFramesIsBlendedThroughTheMaskAsAFramesIs)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ViewHandle mask = leftToRightMask(context->engineSession());
    ASSERT_NE(target, nullptr);
    ASSERT_NE(mask, nullptr);
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(target->isMasked() && target->isMaskEnabled());
    ASSERT_TRUE(settleView(*target));

    std::vector<double> kept;
    {
        SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.alternatives = {20, 40}});
        process->spinBox->setValue(20);
        process->previewCheckbox->setChecked(true);
        ASSERT_TRUE(settleSlider(*process));
        process->spinBox->setValue(40);
        ASSERT_TRUE(settleSlider(*process));
        EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 1u);
        kept = previewSamples(*process);
    }
    std::vector<double> framed;
    {
        SliderHandle process = openSliderProcess(*target);
        process->spinBox->setValue(40);
        process->previewCheckbox->setChecked(true);
        ASSERT_TRUE(settleSlider(*process));
        EXPECT_EQ(process->runTelemetry().keptPreviewsShown, 0u);
        framed = previewSamples(*process);
    }
    ASSERT_EQ(kept.size(), 48u);
    EXPECT_EQ(kept, framed) << "the kept result is not what a frame of its parameters shows";
    EXPECT_NEAR(kept[0], 0.25, 1e-6) << "the mask weighs nothing of the result at the left edge";
    EXPECT_NEAR(kept[7], 0.25 * 1.4, 1e-6) << "and all of it at the right";

    target->removeCurrentMask();
    EXPECT_TRUE(settleView(*target));
}

// What frames kept goes when the target changes: the frame that follows makes every result anew from the new image,
// and nothing kept of the image before is shown.
TEST(PreviewLifecycle, AChangedTargetShowsNothingKeptOfTheImageBefore)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.alternatives = {20, 40}});
    process->spinBox->setValue(20);
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t first = process->runs().size();

    std::optional<an::consumer::ImageAsset> half =
        makeAsset<float>(context->engineSession(), 8u, 6u, Channels::Grey,
                         [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; });
    ASSERT_TRUE(half.has_value());
    target->replaceImage(std::move(*half));
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_GT(process->runs().size(), first) << "no frame ran for the changed image";
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.5 * 1.2, 1e-6);

    const std::size_t afterChange = process->runs().size();
    process->spinBox->setValue(40);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runs().size(), afterChange);
    EXPECT_NEAR(previewSample(*process, 0u, 0u), 0.5 * 1.4, 1e-6) << "a result kept of the image before was shown";
}

// An Apply is followed by one preview of the image it made, with the parameters the reset after it leaves - never one
// of the parameters just applied, shown over their own result.
TEST(PreviewLifecycle, AnApplyIsFollowedByOnePreviewOfTheParametersTheResetLeaves)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    process->spinBox->setValue(30);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t from = process->runs().size();

    process->applyProcess();
    ASSERT_TRUE(settleSlider(*process));
    ASSERT_TRUE(settleView(*target));

    const std::vector<SliderRun> after = process->runs();
    ASSERT_EQ(runsFrom(after, from, an::ui::ProcessRunKind::Apply).size(), 1u);
    const std::vector<SliderRun> previews = runsFrom(after, from);
    ASSERT_EQ(previews.size(), 1u) << "an Apply is followed by exactly one preview";
    EXPECT_EQ(previews[0].value, 0) << "the preview after an Apply shows the parameters the reset left";
    EXPECT_EQ(previews[0].frameKind, an::ui::PreviewFrameKind::Settled);
    EXPECT_EQ(process->spinBox->value(), 0);
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25 * 1.3, 1e-6);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25 * 1.3, 1e-6);
}

// Reset is followed by one preview of the parameters it leaves, however many changes it makes on the way.
TEST(PreviewLifecycle, AResetIsFollowedByOnePreview)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    process->previewCheckbox->setChecked(true);
    process->spinBox->setValue(30);
    ASSERT_TRUE(settleSlider(*process));
    const std::size_t from = process->runs().size();

    QAbstractButton* reset = process->buttons->button(QDialogButtonBox::Reset);
    ASSERT_NE(reset, nullptr);
    reset->click();
    ASSERT_TRUE(settleSlider(*process));

    const std::vector<SliderRun> previews = runsFrom(process->runs(), from);
    ASSERT_EQ(previews.size(), 1u) << "a reset is followed by exactly one preview";
    EXPECT_EQ(previews[0].value, 0);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25, 1e-6);
}

// A frame that fails says why in the window's banner, never in a box; a drag that fails at every step says it once, a
// new cause replaces it, and the first frame shown takes it away.
TEST(PreviewLifecycle, AFailedFrameIsReportedInTheBannerOnceForEachCauseAndNeverInABox)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target);
    // The banner is part of the window as it is shown.
    process->show();
    process->setFailure("the first cause");
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().previewsFailed, 1u);
    EXPECT_EQ(process->runTelemetry().failuresReported, 1u);
    EXPECT_TRUE(process->processMessage().contains(QStringLiteral("the first cause")))
        << process->processMessage().toStdString();

    for (const int value : {10, 20, 30})
    {
        process->spinBox->setValue(value);
        ASSERT_TRUE(settleSlider(*process));
    }
    EXPECT_EQ(process->runTelemetry().previewsFailed, 4u);
    EXPECT_EQ(process->runTelemetry().failuresReported, 1u) << "the same cause was reported again";

    process->setFailure("another cause");
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().failuresReported, 2u);
    EXPECT_TRUE(process->processMessage().contains(QStringLiteral("another cause")))
        << process->processMessage().toStdString();
    EXPECT_EQ(context->errorCount(), 0) << "a failed preview opened a box";

    process->setFailure(std::string());
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_TRUE(process->processMessage().isEmpty()) << "the banner still reports a failure the preview no longer has";
    EXPECT_GE(process->runTelemetry().previewsCommitted, 1u);
}

// The waiting spinner is shown only for a frame still running once its delay has passed, and hidden when the frames
// end; a quick frame shows none.
TEST(PreviewLifecycle, TheWaitingSpinnerIsShownOnlyForAFrameThatRunsPastItsDelay)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = quarterTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.spinner = true});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleSlider(*process));
    process->spinBox->setValue(10);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_EQ(process->runTelemetry().spinnersShown, 0u) << "a quick frame showed the spinner";
    EXPECT_FALSE(process->spinnerSpinning());

    process->gate.close();
    const int arrived = process->gate.arrived();
    process->spinBox->setValue(20);
    ASSERT_TRUE(pumpUntil([&process, arrived]() { return process->gate.arrived() > arrived; }));
    pumpFor(100ms);
    EXPECT_FALSE(process->spinnerSpinning()) << "the spinner was shown before its delay";
    ASSERT_TRUE(pumpUntil([&process]() { return process->spinnerSpinning(); })) << "a long frame showed no spinner";
    EXPECT_EQ(process->runTelemetry().spinnersShown, 1u);

    process->gate.open();
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_FALSE(process->spinnerSpinning()) << "the spinner outlived the frame";
    EXPECT_EQ(process->runTelemetry().spinnersShown, 1u);
}

// A drag of a process whose interactive frames run over what the preview view paints leaves the identity and comes back
// to it in one motion: those frames were shown by their presentation alone over the shared target, so the frame of the
// identity must show the target itself again, not the last frame's bytes.
TEST(PreviewLifecycle, ADragBackToTheIdentityShowsTheTargetAgain)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(8u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = gradientTarget<std::uint16_t>(context->engineSession(), Channels::Rgb);
    ASSERT_NE(target, nullptr);
    SliderHandle process = openSliderProcess(*target, SliderProcessOptions{.identityAtZero = true, .decimated = true});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(zoomPreview(*process, 0.25));
    ASSERT_TRUE(showsTheTargetItself(*process, *target));
    ImageViewWidget* widget = process->previewView()->getViewWidget();
    ASSERT_NE(widget, nullptr);
    const auto ofTarget = widget->presentedImage();
    ASSERT_NE(ofTarget, nullptr) << "the preview view painted nothing of the target";
    const std::vector<std::uint8_t> targetBytes(ofTarget->data(), ofTarget->data() + ofTarget->byteCount());

    const ProcessRunTelemetry before = process->runTelemetry();
    process->slider->setSliderDown(true);
    process->slider->setValue(40);
    ASSERT_TRUE(pumpUntil([&process, &before]() {
        return process->runTelemetry().interactivePreviewsCommitted > before.interactivePreviewsCommitted;
    }));
    const auto ofFrame = widget->presentedImage();
    ASSERT_NE(ofFrame, nullptr);
    ASSERT_TRUE(ofFrame->byteCount() != targetBytes.size() ||
                std::memcmp(ofFrame->data(), targetBytes.data(), targetBytes.size()) != 0)
        << "the interactive frame did not change what the view paints";
    EXPECT_TRUE(showsTheTargetItself(*process, *target))
        << "an interactive frame over a reduction replaced the shared document";

    process->slider->setValue(0);
    ASSERT_TRUE(pumpUntil(
        [&process, &before]() { return process->runTelemetry().identityPreviews > before.identityPreviews; }));
    process->slider->setSliderDown(false);
    ASSERT_TRUE(settleSlider(*process));
    EXPECT_TRUE(showsTheTargetItself(*process, *target));

    const auto after = widget->presentedImage();
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->byteCount(), targetBytes.size());
    EXPECT_EQ(std::memcmp(after->data(), targetBytes.data(), targetBytes.size()), 0)
        << "at the identity the preview view still paints the last interactive frame";
    EXPECT_TRUE(widget->presentationLayerRegion().isNull()) << "a frame's layer is still painted at the identity";
}
