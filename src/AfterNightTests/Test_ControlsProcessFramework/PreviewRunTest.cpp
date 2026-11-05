// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The real-time preview over the framework: one preview when the window opens, a burst of changes drained to one more
/// run with the latest parameters, a preview refused when it is turned off, when its target's document is let go or
/// when its target changes under it, the channel a preview runs on snapshotted when it starts, the preview area run
/// over its crop and shown over the whole image, and a window closed while a preview runs.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace
{

using namespace an::tests::processes;

/// A grey 8 by 6 Float32 target whose samples are `value`.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value,
                                    const std::string& name = "target")
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [value](std::uint32_t, std::uint32_t) { return value; });
    EXPECT_TRUE(document.has_value());
    return makeTargetView(name, std::move(*document));
}

/// Waits, without turning the event loop, until a condition holds: what lets a worker finish before the GUI thread
/// sees its completion.
[[nodiscard]] bool waitWithoutEvents(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

} // namespace

TEST(PreviewRunTest, OpeningThePreviewRunsOnePreviewAndShowsItsResult)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));

    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 3u, 2u), 0.5, 1e-6);
    // The target is untouched by a preview.
    EXPECT_NEAR(documentSample(target->document(), 3u, 2u), 0.25, 1e-6);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsStarted, 1u);
    EXPECT_EQ(telemetry.previewsCommitted, 1u);
    EXPECT_EQ(telemetry.previewsDiscarded, 0u);
    // The original the preview compares with is the target's own image, shared rather than copied.
    EXPECT_EQ(process->previewView()->alternateAsset(), target->document().asset());
}

// A preview takes its read lease before it asks to be admitted. An edit of the view holds the target's writer and
// then asks to be admitted, and an edit the view runs itself does not stop the preview, so at a width of one a preview
// admitted first would hold the only thread while it waited for the writer, and the edit would wait for the thread.
TEST(PreviewRunTest, APreviewWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(1u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    ASSERT_TRUE(settleViewAndSnapshots(*target, context->engineSession()));

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = context->engineSession().admission().admitted;
    process->previewCheckbox->setChecked(true);
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(context->engineSession(), admittedBefore))
        << "the preview was admitted while it could not read the target";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the preview held while the preview waited for the edit's writer";

    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_EQ(process->runTelemetry().previewsCommitted, 1u);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 3u, 2u), 0.5, 1e-6);
    ASSERT_TRUE(settleView(*target));
}

TEST(PreviewRunTest, ABurstOfChangesWhileAPreviewRunsDrainsToOneMoreRunWithTheLatestParameters)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    for (const double factor : {3.0, 4.0, 5.0})
    {
        process->setParams(TestParams{.factor = factor});
    }
    pumpQuietly();
    // The running preview is not stopped by the changes: it finishes, and none of them has started yet.
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1);

    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_DOUBLE_EQ(runs[0].params.factor, 2.0);
    EXPECT_DOUBLE_EQ(runs[1].params.factor, 5.0);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 1.25, 1e-6);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsRequested, 4u);
    EXPECT_EQ(telemetry.previewsStarted, 2u);
    EXPECT_EQ(telemetry.previewsCommitted, 2u);
}

TEST(PreviewRunTest, TurningThePreviewOffStopsTheOneRunningAndShowsNothingOfIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    process->previewCheckbox->setChecked(false);
    // The body sees its stop at the gate, which stays closed.
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->previewView(), nullptr);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsCommitted, 0u);
    EXPECT_EQ(telemetry.previewsDiscarded, 1u);
    EXPECT_EQ(context->errorCount(), 0);
}

TEST(PreviewRunTest, APreviewOfADocumentTheTargetLetGoIsRefusedAndTheNewOneIsPreviewed)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    // The target shows another document: the one the preview reads is closed, so its ticket is refused.
    std::optional<an::ui::ImageDocument> other =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.1f; });
    ASSERT_TRUE(other.has_value());
    target->setDocument(std::move(*other));

    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsDiscarded, 1u);
    EXPECT_EQ(telemetry.previewsCommitted, 1u);
    // What is shown is the new document's preview, never the old one's.
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.2, 1e-6);
}

TEST(PreviewRunTest, APreviewOfATargetThatChangedUnderItIsNeverShown)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    const an::ui::DocumentRevision read = target->document().revision();

    // The target's image is replaced while the preview reads it: the replacement waits for the preview's lease.
    std::optional<an::consumer::ImageAsset> replacement =
        makeAsset<float>(context->engineSession(), 8u, 6u, Channels::Grey,
                         [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.4f; });
    ASSERT_TRUE(replacement.has_value());
    target->replaceImage(std::move(*replacement), true);
    process->gate.open();
    // The preview's worker finishes and the replacement lands before the GUI thread hears of either.
    ASSERT_TRUE(waitWithoutEvents([&target, &read]() { return target->document().revision() != read; }));

    ASSERT_TRUE(settleProcess(*process));
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 2u);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsDiscarded, 1u);
    EXPECT_EQ(telemetry.previewsCommitted, 1u);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.8, 1e-6);
}

TEST(PreviewRunTest, TheChannelAPreviewRunsOnIsTheOneTheSelectorNamedWhenItStarted)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1f * static_cast<float>(c + 1u); });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("colour", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.perChannel = true});
    process->channelSelector->setCurrentIndex(process->channelSelector->findText(QStringLiteral("Green")));
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    // The selector changes while the preview runs on the green channel; the running preview keeps green.
    process->channelSelector->setCurrentIndex(process->channelSelector->findText(QStringLiteral("Blue")));
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].channel, an::ChannelScope::Green);
    EXPECT_EQ(runs[1].channel, an::ChannelScope::Blue);
    // The dispatch extracted the channel each run snapshotted, not the one the selector named when it got there.
    const std::vector<double> planes = process->iterationSamples();
    ASSERT_EQ(planes.size(), 2u);
    EXPECT_NEAR(planes[0], 0.2, 1e-6);
    EXPECT_NEAR(planes[1], 0.3, 1e-6);
    const an::ui::ImageDocument& shown = process->previewView()->document();
    EXPECT_NEAR(documentSample(shown, 0u, 0u, 0u), 0.1, 1e-6);
    EXPECT_NEAR(documentSample(shown, 0u, 0u, 1u), 0.2, 1e-6);
    EXPECT_NEAR(documentSample(shown, 0u, 0u, 2u), 0.6, 1e-6);
}

TEST(PreviewRunTest, APreviewAreaRunsOverItsCropAndIsShownOverTheWholeImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = greyDocument(
        context->engineSession(), 8u, 6u, [](std::uint32_t x, std::uint32_t y) { return 0.01f * (x + 10.0f * y); });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.preview = true, .previewArea = true, .previewAreaSize = QSize(4, 2)});
    ASSERT_TRUE(settleProcess(*process));

    const QRect area = process->previewArea();
    EXPECT_EQ(area, QRect(2, 2, 4, 2));
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].sourceWidth, 4u);
    EXPECT_EQ(runs[0].sourceHeight, 2u);
    const an::ui::ImageDocument& shown = process->previewView()->document();
    ASSERT_TRUE(shown.descriptor().has_value());
    EXPECT_EQ(shown.descriptor()->extent().width(), 8u);
    EXPECT_EQ(shown.descriptor()->extent().height(), 6u);
    // Inside the area the process's result, outside it the image as it is.
    EXPECT_NEAR(documentSample(shown, 2u, 2u), 2.0 * 0.22, 1e-6);
    EXPECT_NEAR(documentSample(shown, 5u, 3u), 2.0 * 0.35, 1e-6);
    EXPECT_NEAR(documentSample(shown, 1u, 2u), 0.21, 1e-6);
    EXPECT_NEAR(documentSample(shown, 6u, 3u), 0.36, 1e-6);
    EXPECT_NEAR(documentSample(shown, 2u, 4u), 0.42, 1e-6);

    // A second preview over the same image is pasted into the same base.
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_NEAR(documentSample(process->previewView()->document(), 2u, 2u), 3.0 * 0.22, 1e-6);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.0, 1e-6);
}

TEST(PreviewRunTest, APreviewAreaWhoseResultIsColourIsShownOverTheGreyImageInColour)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.previewArea = true, .previewAreaSize = QSize(4, 2)});
    process->setParams(TestParams{.body = TestBody::MakeColour, .factor = 2.0});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));

    const an::ui::ImageDocument& shown = process->previewView()->document();
    ASSERT_TRUE(shown.descriptor().has_value());
    EXPECT_EQ(shown.descriptor()->extent().channelCount(), 3u);
    for (std::uint32_t c = 0u; c < 3u; ++c)
    {
        EXPECT_NEAR(documentSample(shown, 3u, 2u, c), 0.5, 1e-6) << c;
        EXPECT_NEAR(documentSample(shown, 0u, 0u, c), 0.25, 1e-6) << c;
    }
}

TEST(PreviewRunTest, APreviewAreaWhoseResultHasAnotherSizeFailsAndSaysWhy)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.previewArea = true, .previewAreaSize = QSize(4, 4)});
    // The banner is part of the window as it is shown.
    process->show();
    process->setParams(TestParams{.body = TestBody::Shrink});
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));

    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsFailed, 1u);
    EXPECT_EQ(telemetry.previewsCommitted, 0u);
    // A preview says why in the window's banner, never in a box.
    EXPECT_EQ(context->errorCount(), 0);
    EXPECT_EQ(telemetry.failuresReported, 1u);
    EXPECT_TRUE(process->processMessage().contains(QStringLiteral("another size")))
        << process->processMessage().toStdString();
    // The preview still shows the target it started with.
    EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), 0.25, 1e-6);
}

TEST(PreviewRunTest, AWindowClosedWhileAPreviewRunsIsHiddenAndClosesWhenItLands)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    EXPECT_FALSE(process->close());
    EXPECT_TRUE(process->isHidden());
    // The preview view outlives the deferred close until the run lands.
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(process->isHidden());
    EXPECT_EQ(process->previewView(), nullptr);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsCommitted, 0u);
    EXPECT_EQ(telemetry.previewsDiscarded, 1u);
}

// At exit the application destroys the process windows still open. A run's work calls its window's process body on a
// worker, so each window stops what it runs and the thread pool is drained before any window is deleted: a window whose
// preview's body waits at its gate is destroyed only once no work is left on the pool, and the stop, not the gate, ends
// the body.
TEST(PreviewRunTest, AWindowDestroyedAtExitIsDestroyedOnlyOnceNoWorkIsLeftRunning)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    const auto record = std::make_shared<ExitRecord>();
    process->exitRecord = record;
    process->show();
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));

    // Should the stop never reach the body, the gate opens after a while rather than hold the case forever.
    std::promise<void> returned;
    std::atomic<bool> openedByWatchdog{false};
    std::thread watchdog([&gate = process->gate, &openedByWatchdog, ended = returned.get_future()]() {
        if (ended.wait_for(std::chrono::seconds(30)) == std::future_status::timeout)
        {
            openedByWatchdog = true;
            gate.open();
        }
    });
    an::ui::destroyProcessWindowsAtExit({QPointer<TopLevelProcessBaseControl>(process.release())});
    returned.set_value();
    watchdog.join();

    EXPECT_TRUE(record->destroyed);
    EXPECT_EQ(record->poolActiveWhenDestroyed, 0) << "a window was deleted while work of it still ran";
    EXPECT_FALSE(openedByWatchdog) << "the stop did not reach the body";
}

// A preview whose work throws outside its body's own guard - the process's result hook here, a reduction or a histogram
// out of memory in use - ends as a failure the window says, and the next request runs a frame: the preview is not left
// running for the window's life.
TEST(PreviewRunTest, APreviewWhoseWorkThrowsEndsAsAFailureAndTheNextOneRuns)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->throwWhenReady = true;
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1);
    EXPECT_EQ(process->runTelemetry().previewsFailed, 1u);
    EXPECT_EQ(process->runTelemetry().previewsCommitted, 0u);

    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 2) << "the preview was left running";
    EXPECT_EQ(process->runTelemetry().previewsCommitted, 1u);
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_NEAR(documentSample(process->previewView()->document(), 3u, 2u), 0.75, 1e-6);
}

TEST(PreviewRunTest, ChangesBeforeThePreviewExistsRunNothingAndOpeningItRunsOnce)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->setParams(TestParams{.factor = 3.0});
    process->setParams(TestParams{.factor = 4.0});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 0);

    process->previewCheckbox->setChecked(true);
    process->setParams(TestParams{.factor = 5.0});
    ASSERT_TRUE(settleProcess(*process));
    // The changes made while the preview was being set up are taken by its one first run.
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_DOUBLE_EQ(runs[0].params.factor, 5.0);
}

namespace
{

/// The samples of what a document holds now, every one, as doubles.
[[nodiscard]] std::vector<double> documentSamples(const an::ui::ImageDocument& document, std::uint32_t width,
                                                  std::uint32_t height)
{
    std::vector<double> samples;
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            samples.push_back(documentSample(document, x, y));
        }
    }
    return samples;
}

/// Where the samples of what a document holds now begin, read under a lease.
[[nodiscard]] const void* documentBlock(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue())
    {
        return nullptr;
    }
    return std::visit(
        [](const auto& typed) -> const void* {
            const auto view = typed.view();
            return view.has_value() ? static_cast<const void*>(view->data()) : nullptr;
        },
        lease->image());
}

} // namespace

// A body that declares it writes every sample is handed a destination that starts unwritten - none of the target is
// copied into it - and what its preview shows is, sample for sample, what its Apply writes. A body that declares
// nothing is handed a copy.
TEST(PreviewRunTest, ABodyThatWritesEverySampleIsHandedNoCopyAndItsPreviewIsItsApply)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    constexpr std::uint32_t kWidth = 13u;
    constexpr std::uint32_t kHeight = 7u;
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), kWidth, kHeight, [](std::uint32_t x, std::uint32_t y) {
            return static_cast<float>(x) * 0.0173f + static_cast<float>(y) * 0.0291f + 0.01f;
        });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_FALSE(process->runs().empty());
    EXPECT_TRUE(process->runs().back().destinationHeldSource) << "a body that declares nothing is handed a copy";

    process->setParams(TestParams{.factor = 0.73, .writesEverySample = true});
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);
    ASSERT_EQ(process->runs().back().kind, an::ui::ProcessRunKind::Preview);
    EXPECT_FALSE(process->runs().back().destinationHeldSource);
    const std::vector<double> previewed = documentSamples(process->previewView()->document(), kWidth, kHeight);
    EXPECT_NEAR(previewed[kWidth + 2u], (2.0 * 0.0173 + 0.0291 + 0.01) * 0.73, 1e-6);

    process->applyProcess();
    ASSERT_TRUE(pumpUntil([&process]() { return process->runsOf(an::ui::ProcessRunKind::Apply) == 1; }));
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(settleView(*target));
    // The Apply changes the target, which asks for another preview, so the Apply need not be the last run.
    const auto runs = process->runs();
    const auto applied = std::find_if(runs.rbegin(), runs.rend(),
                                      [](const auto& run) { return run.kind == an::ui::ProcessRunKind::Apply; });
    ASSERT_NE(applied, runs.rend());
    EXPECT_FALSE(applied->destinationHeldSource);
    EXPECT_EQ(documentSamples(target->document(), kWidth, kHeight), previewed);
}

// While the preview is shown the session keeps the block a frame's result gave back once the next frame replaced it,
// and hands it to the frame after that: consecutive frames of one size alternate between the same two blocks of
// memory, already resident, rather than taking new memory from the platform each time.
TEST(PreviewRunTest, ConsecutivePreviewFramesAlternateBetweenTwoBlocksOfMemory)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    // Four mebibytes of samples: a block the session keeps.
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 1024u, 1024u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);

    std::vector<const void*> blocks;
    for (int frame = 0; frame < 6; ++frame)
    {
        process->setParams(TestParams{.factor = 1.0 + 0.125 * frame, .writesEverySample = true});
        ASSERT_TRUE(settleProcess(*process));
        blocks.push_back(documentBlock(process->previewView()->document()));
        EXPECT_NEAR(documentSample(process->previewView()->document(), 512u, 300u), 0.25 * (1.0 + 0.125 * frame), 1e-6);
    }
    for (std::size_t frame = 2u; frame < blocks.size(); ++frame)
    {
        EXPECT_EQ(blocks[frame], blocks[frame - 2u]) << "frame " << frame;
        EXPECT_NE(blocks[frame], blocks[frame - 1u]) << "frame " << frame;
    }
    EXPECT_GE(context->engineSession().recycledMemory().reusedBlocks, 4u);

    process->previewCheckbox->setChecked(false);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(context->engineSession().recycledMemory().keptBlocks, 0u)
        << "with the preview off, what its frames kept went back to the platform";
}

namespace
{

/// A grey 1024 by 1024 Float32 target at a quarter whose explicit mask - the rule its metadata states - rejects its
/// first ten rows: a mask of 128 KiB, a block the session keeps.
[[nodiscard]] ViewHandle maskedTarget(const an::consumer::EngineSession& session)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    if (!hold.hasValue())
    {
        return ViewHandle();
    }
    const std::optional<an::ImageDescriptor> described =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::Float32, 1024u, 1024u, Channels::Grey);
    EXPECT_TRUE(described.has_value());
    if (!described.has_value())
    {
        return ViewHandle();
    }
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{
        .validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})});
    EXPECT_TRUE(semantics.hasValue());
    if (!semantics.hasValue())
    {
        return ViewHandle();
    }
    an::Result<an::Image<float>> image = an::Image<float>::createFilled(
        hold->engine(), *described, an::StorageArrangement::Planar, 0.25f,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                  an::CodecMetadata{}, std::nullopt));
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(hold->engine(), described->extent());
    EXPECT_TRUE(image.hasValue() && mask.hasValue());
    if (!image.hasValue() || !mask.hasValue())
    {
        return ViewHandle();
    }
    if (const std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view(); bits.has_value())
    {
        for (std::uint32_t y = 0u; y < 10u; ++y)
        {
            for (std::uint32_t x = 0u; x < 1024u; ++x)
            {
                (void)bits->setValid(0u, x, y, false);
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)), std::move(*mask));
    EXPECT_TRUE(asset.hasValue());
    if (!asset.hasValue())
    {
        return ViewHandle();
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset));
    EXPECT_TRUE(document.hasValue());
    return document.hasValue() ? makeTargetView("masked", std::move(*document)) : ViewHandle();
}

/// Where the bits of the mask a document's image states lie, or null.
[[nodiscard]] const void* documentMaskBlock(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue() || lease->validity() == nullptr)
    {
        return nullptr;
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> bits = lease->validity()->view();
    return bits.has_value() ? static_cast<const void*>(bits->data()) : nullptr;
}

} // namespace

// A frame's result carries the target's mask, copied into memory the session keeps while the preview is shown, as the
// result's samples are: consecutive frames alternate their masks between the same two blocks rather than taking new
// memory each time.
TEST(PreviewRunTest, ConsecutivePreviewFramesOfAMaskedTargetAlternateTheirMasksBetweenTwoBlocks)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = maskedTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_NE(process->previewView(), nullptr);

    std::vector<const void*> masks;
    for (int frame = 0; frame < 6; ++frame)
    {
        process->setParams(TestParams{.factor = 1.0 + 0.125 * frame});
        ASSERT_TRUE(settleProcess(*process));
        masks.push_back(documentMaskBlock(process->previewView()->document()));
        ASSERT_NE(masks.back(), nullptr) << "frame " << frame << " carries no mask";
    }
    for (std::size_t frame = 2u; frame < masks.size(); ++frame)
    {
        EXPECT_EQ(masks[frame], masks[frame - 2u]) << "frame " << frame;
        EXPECT_NE(masks[frame], masks[frame - 1u]) << "frame " << frame;
    }
}

// A frame's result waits for no reader of the image the preview shows: what reads it - the lens, the statistics - keeps
// reading it, and the frame is shown at once rather than by a worker that waits for the reader to let go.
TEST(PreviewRunTest, AFrameIsShownWhileAReaderHoldsTheImageThePreviewShows)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_FALSE(preview->document().isShared()) << "the preview shows a document of its own";

    an::Result<an::consumer::AssetReadLease> reading = preview->document().read();
    ASSERT_TRUE(reading.hasValue());
    const std::uint64_t read = reading->assetIdentity();
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process)) << "the frame waited for the reader";
    EXPECT_NE(preview->document().revision().asset.assetIdentity, read);
    EXPECT_NEAR(documentSample(preview->document(), 3u, 2u), 0.75, 1e-6);
    EXPECT_EQ(reading->assetIdentity(), read);
    const double held = std::visit(
        [](const auto& typed) -> double {
            const auto view = typed.view();
            const auto* sample = view.has_value() ? view->sampleAt(0u, 3u, 2u, 0u) : nullptr;
            return sample != nullptr ? static_cast<double>(*sample) : -1.0;
        },
        reading->image());
    EXPECT_NEAR(held, 0.5, 1e-6) << "the reader saw its image change under it";
}

TEST(PreviewRunTest, ThePreviewHoldsTheSessionsPreviewReservationWhileItIsShown)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_GT(context->engineSession().admission().previewReservation, 0u);

    process->previewCheckbox->setChecked(false);
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(context->engineSession().admission().previewReservation, 0u);
}
